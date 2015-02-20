#ifndef LEXLSI
#define LEXLSI

#include <lexlse.h>
#include <objective.h>
#include <cycling.h>

namespace LexLS
{
    namespace internal
    {    
        /** 
            \brief Definition of a lexicographic least-squares problem with inequality constraints

            \todo When we solve a sequence of LexLSI problems we could specify the maximum size of the
            envisioned objectives so that we don't have to allocate memory online.

            \todo To use a structure containing the tolerances.
        */
        class LexLSI
        {
        public:

            // ---------------------------------------------------------------------------------
            // Constructors
            // ---------------------------------------------------------------------------------

            /** 
                \param[in] nVar_    Number of variables (only number of elements in x, and not in the residuals w)
                \param[in] nObj_    Number of objectives
                \param[in] ObjDim_  Number of constraints involved in each objective
                \param[in] ObjType_ Type of each objective
            */
            LexLSI(Index nVar_, Index nObj_, Index *ObjDim_, ObjectiveType *ObjType_):
                nVar(nVar_), 
                nObj(nObj_),
                x0_is_specified(false),
                status(TERMINATION_STATUS_UNKNOWN)
            {           
                parameters.setDefaults();
                setParameters(parameters);

                resize(ObjDim_,ObjType_);
            }

            // ---------------------------------------------------------------------------------

            /** 
                \brief Adds a constraint to the working set (and sets its type)

                \param[in] ObjIndex         Index of objective.
                \param[in] CtrIndex         Index of constraint: objectives[ObjIndex].data.row(CtrIndex).
                \param[in] type             Type of the constraint.

                \note This function will be a part of the interface level and its purpose is to provide
                the initial working set.

                \todo Move all veification of inputs to the API level
            */
            void api_activate(Index ObjIndex, Index CtrIndex, ConstraintActivationType type)
            {
                if (!objectives[ObjIndex].isActive(CtrIndex))
                {
                    // which constraints are considered as CTR_ACTIVE_EQ is determined internaly
                    if (type == CTR_ACTIVE_LB || type == CTR_ACTIVE_UB)
                        activate(ObjIndex, CtrIndex, type, false);
                    else // see setData(...)
                        std::cout << "WARNING: the user cannot define explicitly which constraints are of type CTR_ACTIVE_EQ \n" << std::endl;
                }
            }
      
            /** 
                \brief Adds a constraint to the working set (and sets its type)

                \param[in] ObjIndex       Index of objective.
                \param[in] CtrIndex       Index of constraint: objectives[ObjIndex].data.row(CtrIndex).
                \param[in] type           Type of the constraint.
                \param[in] CountIteration if true, the iteration counter #nActivations is incremented

                \note CountIteration = false is used when specifying the initial working set
            */
            void activate(Index ObjIndex, Index CtrIndex, ConstraintActivationType type, bool CountIteration=true)
            {
                if (ObjIndex >= nObj)                
                    throw Exception("ObjIndex >= nObj");

                objectives[ObjIndex].activate(CtrIndex, type);

                if (CountIteration)
                    nActivations++;
            }
        
            /** 
                \brief Removes a constraint from the working set

                \param[in] ObjIndex       Index of objective.
                \param[in] CtrIndexActive Index of constraint: objectives[ObjIndex].working_set.active[CtrIndexActive].
            */
            void deactivate(Index ObjIndex, Index CtrIndexActive)
            {
                if (ObjIndex >= nObj)                
                    throw Exception("ObjIndex >= nObj");

                objectives[ObjIndex].deactivate(CtrIndexActive);

                nDeactivations++;
            }

            /**
               \brief Computes an initial feasible pair (x,w)
            */
            void phase1()
            {         
                bool active_constraints_exist = false;
                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                {
                    // we would enter even if there are only equality constraints
                    if (objectives[ObjIndex].getActiveCtrCount() > 0)
                    {
                        active_constraints_exist = true;
                        break;
                    }
                }
         
                // --------------------------------------------------------
                // form x
                // --------------------------------------------------------
                if (active_constraints_exist)
                {
                    formLexLSE();                
                
                    if (!x0_is_specified)
                    {
                        lexlse.factorize();
                        lexlse.solve();                    
                        x = lexlse.get_x();

                        nFactorizations++;
                    }
                }
                else
                {
                    if (!x0_is_specified)
                    {
                        for (Index k=0; k<nVar; k++)
                            x(k) = 0.01; // set to something different from 0
                    }
                }
            
                // --------------------------------------------------------
                // form w
                // --------------------------------------------------------
                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    objectives[ObjIndex].phase1(x);

                // --------------------------------------------------------
                // form step (similar to formStep())
                // --------------------------------------------------------
                dx.setZero();
                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    objectives[ObjIndex].formStep(dx);
                // --------------------------------------------------------
            }
        
            /**
               \brief solve a LexLSI problem

               \return the termination reason
            */
            TerminationStatus solve()
            {
                OperationType operation;

                phase1();

                if (!parameters.output_file_name.empty())
                    outputStuff(parameters.output_file_name.c_str(), OPERATION_UNDEFINED, true);

                while (1)
                {
                    operation = verifyWorkingSet();

                    if (!parameters.output_file_name.empty())
                        outputStuff(parameters.output_file_name.c_str(), operation);
              
                    if ((status == PROBLEM_SOLVED) || (status == PROBLEM_SOLVED_CYCLING_HANDLING))
                    {
                        break; // we are done ...
                    }
                    else
                    {
                        if (nFactorizations >= parameters.max_number_of_factorizations)
                        {
                            status = MAX_NUMBER_OF_FACTORIZATIONS_EXCEDED;
                            break;
                        }
                    }
                }
                return status;
            }

            /**
               \brief Prints some fields
           
               \param[in] field description of field to print.

               \todo Remove this function.
            */
            void print(const char * field)
            {
                if (!strcmp(field, "WorkingSet"))
                {
                    for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                        objectives[ObjIndex].print("WorkingSet");
                    std::cout << std::endl;
                }
                else if (!strcmp(field, "data"))
                {
                    for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    {
                        std::cout << "--------------------------------------------------" << std::endl;
                        std::cout << "Objectives[" << ObjIndex << "].";   
                        objectives[ObjIndex].print("data");
                    }
                    std::cout << std::endl;
                }
                else if (!strcmp(field, "nIterations"))
                {
                    std::cout << "nIterations = " << nIterations 
                              << " (ADD = "       << nActivations 
                              << ", REMOVE = "    << nDeactivations 
                              << ", FACTOR = "    << nFactorizations
                              << ", ACTIVE = "    << getActiveCtrCount() << ")" << std::endl;
                    std::cout << std::endl;
                }
                else if (!strcmp(field, "x"))
                {
                    std::cout << "x = \n" << x << std::endl;
                    std::cout << std::endl;
                }
                else if (!strcmp(field, "w"))
                {
                    for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    {
                        std::cout << "w["<<ObjIndex<<"] = \n" << objectives[ObjIndex].get_v() << std::endl;
                        std::cout << std::endl;
                    }
                    std::cout << std::endl;
                }
            }

            // ---------------------------------------------------------------------------------
            // set & get
            // ---------------------------------------------------------------------------------

            /**
               \brief Sets the initial value for the decision variable x
            */
            void set_x0(dVectorType &x0)
            {
                x = x0;
                x0_is_specified = true;
            }

            /**
               \brief Sets the residual for objective k
            */
            void set_v0(Index ObjIndex, dVectorType &w)
            {
                objectives[ObjIndex].set_v0(w);
            }

            /**
               \brief Sets parameters
            */
            void setParameters(const ParametersLexLSI &parameters_)
            {
                parameters = parameters_;
                ParametersLexLSE lexlse_parameters;

                lexlse_parameters.tol_linear_dependence          = parameters.tol_linear_dependence;
                lexlse_parameters.regularization_type            = parameters.regularization_type;
                lexlse_parameters.max_number_of_CG_iterations    = parameters.max_number_of_CG_iterations;
                lexlse_parameters.variable_regularization_factor = parameters.variable_regularization_factor;

                lexlse.setParameters(lexlse_parameters);

                if (parameters.cycling_handling_enabled)
                {
                    cycling_handler.set_max_counter(parameters.cycling_max_counter);
                    cycling_handler.set_relax_step(parameters.cycling_relax_step);
                }
            }


            /** 
                \brief Set data of objective ObjIndex (ObjType = GENERAL_OBJECTIVE is assumed)

                \param[in] ObjIndex Index of objective
                \param[in] data     [A,LowerBounds,UpperBounds]
            */
            void setData(Index ObjIndex, const dMatrixType& data)
            {
                if (ObjIndex >= nObj)                
                    throw Exception("ObjIndex >= nObj");

                if (objectives[ObjIndex].getObjType() != GENERAL_OBJECTIVE)
                    throw Exception("ObjType = GENERAL_OBJECTIVE is assumed");          

                if (objectives[ObjIndex].getDim() != data.rows())                
                    throw Exception("Incorrect number of equations");

                // check bounds
                RealScalar bl, bu;
                for (Index CtrIndex=0; CtrIndex<objectives[ObjIndex].getDim(); CtrIndex++)
                {    
                    bl = data.coeffRef(CtrIndex,nVar);
                    bu = data.coeffRef(CtrIndex,nVar+1);
                             
                    if (isEqual(bl,bu))
                        activate(ObjIndex,CtrIndex,CTR_ACTIVE_EQ,false);
                    else if (bl > bu)
                        throw Exception("(general) Lower bound is greater than upper bound.");   
                }

                objectives[ObjIndex].setData(data);
            }

            /** 
                \brief Set data of objective ObjIndex (ObjType = SIMPLE_BOUNDS_OBJECTIVE is assumed)

                \param[in] ObjIndex Index of objective
                \param[in] VarIndex Index variables subject to simple bounds
                \param[in] data     [LowerBounds,UpperBounds]
            */
            void setData(Index ObjIndex, Index *VarIndex, const dMatrixType& data)
            {
                if (ObjIndex >= nObj)                
                    throw Exception("ObjIndex >= nObj");

                if (objectives[ObjIndex].getObjType() != SIMPLE_BOUNDS_OBJECTIVE)
                    throw Exception("ObjType = SIMPLE_BOUNDS_OBJECTIVE is assumed");

                if (objectives[ObjIndex].getDim() != data.rows())                
                    throw Exception("Incorrect number of equations");
            
                // check bounds
                RealScalar bl, bu;
                for (Index CtrIndex=0; CtrIndex<objectives[ObjIndex].getDim(); CtrIndex++)
                {    
                    bl = data.coeffRef(CtrIndex,0);
                    bu = data.coeffRef(CtrIndex,1);

                    if (isEqual(bl,bu))
                        activate(ObjIndex,CtrIndex,CTR_ACTIVE_EQ,false);
                    else if (bl > bu)
                        throw Exception("(simple) Lower bound is greater than upper bound.");
                }

                // check whether VarIndex contains repeated indexes (VarIndex is not assumed to be sorted)
                for (Index k=0; k<objectives[ObjIndex].getDim(); k++)
                    for (Index j=0; j<objectives[ObjIndex].getDim(); j++)
                        if ((VarIndex[k] == VarIndex[j]) && (j != k))
                            throw Exception("Elements of VarIndex are not unique.");   
            
                objectives[ObjIndex].setData(VarIndex, data);
            }

            /** 
                \brief Set (a non-negative) regularization factor for objective ObjIndex

                \note Regularization of an objective of type SIMPLE_BOUNDS_OBJECTIVE is not performed
            */        
            void setRegularizationFactor(Index ObjIndex, RealScalar factor)
            {
                // @todo: check whether ObjIndex and factor make sense. 
                
                objectives[ObjIndex].setRegularization(factor);
            }
        
            /** 
                \brief Return the (primal) solution vector
            */
            dVectorType& get_x()
            {
                return x;
            }

            dVectorType& get_v(Index ObjIndex)
            {
                return objectives[ObjIndex].get_v();
            }

            /** 
                \brief Outputs the Lagrange multipliers associated to the constraintes involved in all objectives

                \note The column corresponding to SIMPLE_BOUNDS_OBJECTIVE is stored

                \note The order of the constraints in the active set is preserved. 

                \attention Note that L is returned by value.
            */
            dMatrixType getLambda()
            {
                Index nActiveCtr = 0;
                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    nActiveCtr += lexlse.getDim(ObjIndex); 
            
                dMatrixType L = dMatrixType::Zero(nActiveCtr,nObj);

                Index nMeaningful = lexlse.getFixedVariablesCount();
                for (Index ObjIndex=0; ObjIndex<nObj-nObjOffset; ObjIndex++) // Objectives of LexLSE
                {
                    lexlse.ObjectiveSensitivity(ObjIndex);

                    nMeaningful += lexlse.getDim(ObjIndex);
                    L.col(nObjOffset + ObjIndex).head(nMeaningful) = lexlse.getWorkspace().head(nMeaningful);
                }

                return L;
            }

            /** 
                \brief Get number of cycling relaxations 
            */
            Index getCyclingCounter() const
            {
                return cycling_handler.get_counter();
            }

            /** 
                \brief Returns number of iterations in the active-set method
            */
            Index getFactorizationsCount() const
            {
                return nFactorizations;
            }

            /** 
                \brief Returns number of iterations during which a constraint has been added to the
                working set
            */
            Index getActivationsCount() const
            {
                return nActivations;
            }

            /** 
                \brief Returns number of iterations during which a constraint has been removed to the
                working set
            */
            Index getDeactivationsCount() const
            {
                return nDeactivations;
            }

            /** 
                \brief Returns number of active constraints
            */
            Index getActiveCtrCount() const
            {
                Index n = 0;
                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    n += objectives[ObjIndex].getActiveCtrCount();

                return n;
            }

            /** 
                \brief Outputs the types of active constraints for a given objective
            */
            void getActiveCtr(Index ObjIndex, std::vector<ConstraintActivationType>& ctr_type) const
            {
                Index ind;
                Index dim = objectives[ObjIndex].getDim();
                ctr_type.resize(dim,CTR_INACTIVE);
                for (Index k=0; k<objectives[ObjIndex].getActiveCtrCount(); k++)
                {   
                    ind = objectives[ObjIndex].getActiveCtrIndex(k);
                    ctr_type[ind] = objectives[ObjIndex].getActiveCtrType(k);
                }
            }

            /** 
                \brief Returns number of objectives
            */
            Index getObjectivesCount() const
            {
                return nObj;
            }

            /** 
                \brief Returns number of constraints in objective ObjIndex

                \param[in] ObjIndex Index of objective
            */
            Index getObjDim(Index ObjIndex) const
            {
                return objectives[ObjIndex].getDim();
            }
                
        private:
            /// \brief Parameters of the solver.
            ParametersLexLSI parameters;


            /** 
                \brief Resize LexLSI problem

                \param[in] ObjDim_  Number of constraints involved in each objective
                \param[in] ObjType_ Type of each objective
            */
            void resize(Index *ObjDim_, ObjectiveType *ObjType_)
            {
                nObjOffset = 0;
                if (ObjType_[0] == SIMPLE_BOUNDS_OBJECTIVE) // If only simple bounds in first objective of LexLSI
                    nObjOffset = 1;

                // In LexLSE, fixed variables are handled separately and are not defined as an objective
                // ObjDim_ + nObjOffset is pointer arithmetic
                lexlse.resize(nVar, nObj - nObjOffset, ObjDim_ + nObjOffset);

                nActive.resize(nObj);
                objectives.resize(nObj); 
                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    objectives[ObjIndex].resize(ObjDim_[ObjIndex],nVar,ObjType_[ObjIndex]);

                x.resize(nVar);
                dx.resize(nVar); 

                initialize();
            }

            /** 
                \brief Initializations
            */
            void initialize()
            {            
                nIterations     = 0;
                nActivations    = 0;
                nDeactivations  = 0;
                nFactorizations = 0;

                step_length = 0;

                x.setZero();
                dx.setZero();
            }

            /** 
                \brief Form an LexLSE problem (using the current working set)
            */
            void formLexLSE()
            {
                // obj_info.FirstRowIndex has to be initialized before I start setting CtrType in formLexLSE below
                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    nActive(ObjIndex) = objectives[ObjIndex].getActiveCtrCount();
                lexlse.setObjDim(&nActive(0)+nObjOffset);

                Index counter = 0;
                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    objectives[ObjIndex].formLexLSE(lexlse, counter, ObjIndex-nObjOffset);
            }

            /**
               \brief Form the step (dx,dw) from the current iterate and compute the step length StepLength
            */
            void formStep()
            {            
                dx = lexlse.get_x() - x;
                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    objectives[ObjIndex].formStep(dx);
            }

            /**
               \brief Check for blocking constraints

               \param[out] ObjIndexBlocking Index of objective with the blocking constraint (if such exists).
               \param[out] CtrIndexBlocking Index of blocking constraint (in LexLSI objective ObjIndexBlocking).
               \param[out] CtrTypeBlocking  Type of the blocking constraint.
               \param[out] alpha            scaling factor for the step.

               \return true if there are blocking constraints

               @todo Name of a method 'checkBlockingConstraints()' is shared by
               both this and Objective classes. This may be confusing.
            */      
            bool checkBlockingConstraints(Index &ObjIndexBlocking, 
                                          Index &CtrIndexBlocking, 
                                          ConstraintActivationType &CtrTypeBlocking, 
                                          RealScalar &alpha)
            {
                alpha = 1;
                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    if (objectives[ObjIndex].checkBlockingConstraints(CtrIndexBlocking, CtrTypeBlocking, alpha, parameters.tol_feasibility))
                        ObjIndexBlocking = ObjIndex;
            
                if (alpha < 1)
                    return true; // there are blocking constraints
                else
                    return false;
            }

            /**
               \brief Finds active constraints that should be removed from the working set

               \param[out] ObjIndex2Remove Index of objective from which to remove a constraint
               \param[out] CtrIndex2Remove Index of constraint in the working set of objective ObjIndex2Remove to remove

               \return true if there are constraints to remove
            */
            bool findActiveCtr2Remove(Index &ObjIndex2Remove, Index &CtrIndex2Remove)
            {
                bool DescentDirectionExists = false;
                int ObjIndex2Remove_int;
                for (Index ObjIndex=0; ObjIndex<nObj-nObjOffset; ObjIndex++) // loop over objectives of LexLSE problem
                {
                    DescentDirectionExists = lexlse.ObjectiveSensitivity(ObjIndex, 
                                                                         CtrIndex2Remove, 
                                                                         ObjIndex2Remove_int, 
                                                                         parameters.tol_wrong_sign_lambda,
                                                                         parameters.tol_correct_sign_lambda);

                    if (DescentDirectionExists)
                        break;
                }
            
                // Note that when the first objective of LexLSI is of type SIMPLE_BOUNDS_OBJECTIVE,
                // and if a constraint is to be removed from it, ObjIndex2Remove_int = -1 (see end of LexLSE.ObjectiveSensitivity(...)).
                ObjIndex2Remove = ObjIndex2Remove_int + nObjOffset; // objective of LexLSI problem

                return DescentDirectionExists;
            }

            /**
               \brief One iteration of an active-set method
            */        
            OperationType verifyWorkingSet()
            {
                // ----------------------------------------------------------------------
                Index ObjIndex2Manipulate, CtrIndex2Manipulate;
                ConstraintActivationType CtrType2Manipulate = CTR_INACTIVE;

                bool normalIteration = true;
                OperationType operation = OPERATION_UNDEFINED;
                ConstraintIdentifier constraint_identifier;

                RealScalar alpha;
                // ----------------------------------------------------------------------

                if (nIterations != 0) // nIterations == 0 is handled in phase1()
                {
                    formLexLSE();
                
                    lexlse.factorize();
                    lexlse.solve();
                
                    formStep();

                    nFactorizations++;
                }
                else // if nIterations == 0
                {
                    if (x0_is_specified)
                    {
                        normalIteration = false;
                    }
                }

                if (checkBlockingConstraints(ObjIndex2Manipulate, CtrIndex2Manipulate, CtrType2Manipulate, alpha))
                {
                    if (parameters.cycling_handling_enabled)
                    {
                        constraint_identifier.set(ObjIndex2Manipulate, CtrIndex2Manipulate, CtrType2Manipulate);
                    }

                    operation = OPERATION_ADD;
                    activate(ObjIndex2Manipulate, CtrIndex2Manipulate, CtrType2Manipulate);
                }
                else
                {
                    if (normalIteration) 
                    {
                        if (findActiveCtr2Remove(ObjIndex2Manipulate, CtrIndex2Manipulate))
                        {
                            if (parameters.cycling_handling_enabled)
                            {
                                constraint_identifier.set(ObjIndex2Manipulate, 
                                                         objectives[ObjIndex2Manipulate].getActiveCtrIndex(CtrIndex2Manipulate), 
                                                         objectives[ObjIndex2Manipulate].getActiveCtrType(CtrIndex2Manipulate));
                            }
                            
                            operation = OPERATION_REMOVE;
                            deactivate(ObjIndex2Manipulate, CtrIndex2Manipulate); 
                        }
                        else
                        {
                            status = PROBLEM_SOLVED;
                        }
                    }
                }

                if (operation == OPERATION_ADD)
                    step_length = alpha; // record the value of alpha
                else
                    step_length = -1; // this is used only for debugging purposes

                if (alpha > 0) // take a step
                {
                    x += alpha*dx;
                    for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                        objectives[ObjIndex].step(alpha);
                }

                if (parameters.cycling_handling_enabled && operation != OPERATION_UNDEFINED)
                    status = cycling_handler.update(operation, constraint_identifier, objectives, nIterations, false);

                nIterations++;

                return operation;
            }

            /**
               \brief Outputs resiadual norm to file
            */
            void outputStuff(const char *file_name, OperationType operation, bool flag_clear_file = false)
            {
                // clear the content of the file
                if (flag_clear_file)
                {
                    std::ofstream file(file_name, std::ios::out | std::ofstream::trunc);
                    file.close();
                }

                std::ofstream file(file_name, std::ios::out | std::ios::app);
                file.precision(15);

                if (flag_clear_file)
                    file << "% phase 1 (x0_is_specified = "<<x0_is_specified<<") \n"; 

                if (nIterations == 1)
                    file << "% here lexlse is not solved\n"; 

                file << "% ---------------------------------------------\n"; 
                file << "% nIterations       = " << nIterations << "\n"; 
                file << "% status            = " << status << "\n"; 
                file << "% counter (cycling) = " << getCyclingCounter() << "\n"; 
                file << "operation_("<<nIterations+1<<")       = " << operation << ";\n"; 
                file << "nFactorizations_("<<nIterations+1<<") = " << getFactorizationsCount() << ";\n";
                if (!flag_clear_file)
                    file << "stepLength_("<<nIterations+1<<")      = " << step_length << ";\n";

                if ((getFactorizationsCount() > 0) && nIterations != 1)
                {
                    file << "% ---------------------------------------------\n";
                    file << "% solve lexlse with previous active set \n"; 

                    dVectorType xStar = lexlse.get_x();
                
                    file << "xStar_(:,"<<nIterations+1<<") = [ "; 
                    for (Index k=0; k<nVar; k++)
                        file << xStar(k) << " "; 
                    file << "]'; \n"; 
                }

                file << "% ---------------------------------------------\n";

                if ((x0_is_specified) && (nIterations == 1))
                {
                    // when x0 is specified by the user, the step direction is not recomputed at nIterations == 1
                }
                else
                {
                    file << "dx_(:,"<<nIterations+1<<") = [ "; 
                    for (Index k=0; k<nVar; k++)
                        file << dx(k) << " "; 
                    file << "]'; \n"; 

                    for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    {
                        dVectorType dw_ = objectives[ObjIndex].get_dv();
                    
                        file << "dw_{"<<ObjIndex+1<<"}(:,"<<nIterations+1<<") = [ "; 
                        for (Index k=0; k<objectives[ObjIndex].getDim(); k++)
                        {
                            file << dw_(k) << " ";
                        }
                        file << "]';\n";
                    }
                }

                file << "x_(:,"<<nIterations+1<<") = [ "; 
                for (Index k=0; k<nVar; k++)
                    file << x(k) << " "; 
                file << "]'; \n"; 

                for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                {
                    dVectorType w_ = objectives[ObjIndex].get_v();

                    file << "w_{"<<ObjIndex+1<<"}(:,"<<nIterations+1<<") = [ "; 
                    for (Index k=0; k<objectives[ObjIndex].getDim(); k++)
                    {
                        file << w_(k) << " ";
                    }
                    file << "]';\n";
                }
            
                if ((x0_is_specified) && (nIterations == 1))
                {
                    // when x0 is specified by the user, the step direction is not recomputed at nIterations == 1
                }
                else
                {
                    file << "% ---------------------------------------------\n";
                    for (Index ObjIndex=0; ObjIndex<nObj; ObjIndex++)
                    {
                        file << "a_{"<<ObjIndex+1<<"}(:,"<<nIterations+1<<") = [ "; 
                        for (Index k=0; k<objectives[ObjIndex].getDim(); k++)
                            file << (Index) objectives[ObjIndex].getCtrType(k) << " ";
                        file << "]';\n";
                    }
                }

                /*
                  dMatrixType L = getLambda();
            
                  file << "L_{"<<nIterations+1<<"} = [";
                  for (Index i=0; i<L.rows(); i++)
                  {
                  for (Index j=0; j<L.cols(); j++)
                  {
                  file << L(i,j) << " ";
                  }
                  file << "\n";
                  }
                  file << "]; \n";
                */
            
                file << "\n";

                file.close();
            }

            // ==================================================================
            // definition of scalars
            // ==================================================================

            /** 
                \brief Number of decision variables #x

                \note If we consider the problem: minimize_{x,w} norm(w,2)^2, subject to A*x - b = w,
                then clearly w is a decision variable as well, but we could always think of this propblem in
                terms of: minimize_{x} norm(A*x-b,2)^2.
            */
            Index nVar;  
    
            /** 
                \brief Number of objectives
            */
            Index nObj;

            /** 
                \brief When the objective with highest priority of LexLSI has only simple bounds (i.e.,
                its ObjType = SIMPLE_BOUNDS_OBJECTIVE), the number of objectives in LexLSI and LexLSE differ
                with 1 because fixed variables are not treated as an objective in LexLSE.
            */
            Index nObjOffset;

            /*
              \brief Number of iterations during which a constraint was added
            */
            Index nActivations;

            /*
              \brief Number of iterations during which a constraint was removed
            */
            Index nDeactivations;

            /*
              \brief Number of factorization
            */
            Index nFactorizations;

            /*
              \brief Iterations counter
            */
            Index nIterations;

            /** 
                \brief If x0_is_specified == true, the function set_x0(dVectorType &x0) has been
                called and x0 has been initialized.

                \note This is later used in phase1().
            */
            bool x0_is_specified;

            /*
              \brief Equal to alpha in verifyWorkingSet()

              \note For output/debugging purposes
            */
            RealScalar step_length;
    
            // ==================================================================
            // definition of vectors
            // ==================================================================

            /** 
                \brief The current value of the decision variables - not including the residual
            */
            dVectorType x;

            /** 
                \brief The current descent direction from #x
            */
            dVectorType dx;

            /** 
                \brief Number of active constraints in each objective
            
                \note This variable is used for convenience. 
            */
            iVectorType nActive;
        
            // ==================================================================
            // other definitions
            // ==================================================================

            /** 
                \brief Provides information about the reson for termination
            */
            TerminationStatus status;

            /** 
                \brief Handles the lexicographic least-squares problem with equality constraints

                \note This instance of LexLSE is used to solve multiplie problems - it is initialized
                with the largest expected problem dimensions. 
            */       
            LexLSE lexlse;

            /** 
                \brief Vector of objectives
            */       
            std::vector<Objective> objectives;

            /** 
                \brief Handle cycling
            */       
            CyclingHandler cycling_handler;

        }; // END class LexLSI 

    } // END namespace internal

} // END namespace LexLS 

#endif // LEXLSE
