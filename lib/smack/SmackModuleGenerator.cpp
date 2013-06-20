//
// Copyright (c) 2008 Zvonimir Rakamaric (zvonimir@cs.utah.edu)
// This file is distributed under the MIT License. See LICENSE for details.
//
#include "SmackModuleGenerator.h"

namespace smack {

    llvm::RegisterPass<SmackModuleGenerator> X("smack", "SMACK generator pass");
    char SmackModuleGenerator::ID = 0;

    bool SmackModuleGenerator::runOnModule(llvm::Module &m) {

        SmackRep* rep = SmackRepFactory::createSmackRep(&getAnalysis<llvm::DataLayout>());
        program = new Program(rep->getPrelude());

        DEBUG(errs() << "Analyzing globals...\n");
        rep->declareGlobals(m, program);

        // AXIOMS about variable uniqueness
        // NOTE: This should be covered by the "unique" annotation on the
        // declarations.  What is not covered is that their REFERENCES should
        // be unique...        
        // for (unsigned i=0; i<globals.size(); i++)
        //     for (unsigned j=i+1; j<globals.size(); j++)
        //         program->addDecl(new AxiomDecl(
        //             Expr::neq(Expr::id(globals[i]), Expr::id(globals[j])) ));

        DEBUG(errs() << "Analyzing functions...\n");
        
        set<pair<llvm::Function*,int> > missingDecls;

        for (llvm::Module::iterator func = m.begin(), e = m.end(); 
                func != e; ++func) {

            string name = rep->id(func);
            
            if (func->isDeclaration() || rep->isSmackName(name))
                continue;
            
            program->addDecl(new ConstDecl(name, rep->getPtrType(), true));
//            program->addDecl(new AxiomDecl(
//                Expr::fn(SmackRep::STATIC, rep.obj(Expr::id(name))) ));
        
            DEBUG(errs() << "Analyzing function: " << name << "\n");

            Procedure *proc = new Procedure(name);
            program->addProc(proc);
            
            // PARAMETERS
            for (llvm::Function::const_arg_iterator
                    arg = func->arg_begin(), e = func->arg_end(); arg != e; ++arg) {
                proc->addParam(rep->id(arg), rep->type(arg->getType()));
            }
        
            // RETURNS
            if (! func->getReturnType()->isVoidTy() )
                proc->addRet(SmackRep::RET_VAR, rep->type(func->getReturnType()));
        
            // MODIFIES
            rep->addModifies(proc);

            // BODY
            if ( !func->isDeclaration() && !func->empty() 
                && !func->getEntryBlock().empty() ) {

                map<const llvm::BasicBlock*, Block*> known;
                stack<llvm::BasicBlock*> workStack;
                SmackInstGenerator igen(rep, *proc, known, missingDecls);

                llvm::BasicBlock& entry = func->getEntryBlock();
                workStack.push(&entry);
                known[&entry] = igen.createBlock();

                // INVARIANT: knownBlocks.CONTAINS(b) iff workStack.CONTAINS(b)
                // or workStack.CONTAINED(b) at some point in time.
                while (!workStack.empty()) {      
                    llvm::BasicBlock *b = workStack.top();
                    workStack.pop();
                
                    for (llvm::succ_iterator s = succ_begin(b),
                            e = succ_end(b); s != e; ++s) {
          
                        // uncovered basic block
                        if (known.count(*s) == 0) {
                            known[*s] = igen.createBlock();
                            workStack.push(*s);
                        }
                    }

                    // NOTE: here we are sure that all successor blocks have
                    // already been created, and are mapped for the visitor.

                    igen.setCurrBlock(known[b]);
                    igen.visit(b);
                }
            }

            DEBUG(errs() << "Finished analyzing function: " << name << "\n\n");
        }

        // Add any missing procedure declarations 
        // NOTE: for the moment, these correspond to VARARG procedures.
        for (set<pair<llvm::Function*,int> >::iterator d = missingDecls.begin();
            d != missingDecls.end(); ++d) {
            llvm::Function* func = d->first;
            int numArgs = d->second;

            stringstream name;
            name << rep->id(func);
            if (func->isVarArg() && numArgs > 0) {
                name << "#" << numArgs;
            }

            Procedure *p = new Procedure(name.str());

            if (func->isVarArg()) {
                for (int i=0; i<numArgs; i++) {
                    stringstream param;
                    param << "p" << i;
                    p->addParam(param.str(), rep->getPtrType());
                }
            } else {
                int i = 0;
                for (llvm::Function::const_arg_iterator
                        arg = func->arg_begin(), e = func->arg_end(); arg != e; ++arg, ++i) {
                    stringstream param;
                    param << "p" << i;
                    p->addParam(param.str(), rep->type(arg->getType()));
                }
            }
 
            if (! func->getReturnType()->isVoidTy() )
                p->addRet(SmackRep::RET_VAR, rep->getPtrType());
            program->addProc(p);
        }
        
        return false;
    }

} // namespace smack
