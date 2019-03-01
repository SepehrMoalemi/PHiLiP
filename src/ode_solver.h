#ifndef __ODESOLVER_H__
#define __ODESOLVER_H__

#include <deal.II/lac/vector.h>

#include <deal.II/lac/trilinos_sparse_matrix.h>
#include <deal.II/lac/trilinos_precondition.h>
#include <deal.II/lac/trilinos_solver.h>

#include "parameters.h"
#include "dg.h"


namespace PHiLiP
{
    using namespace dealii;


    template <int dim, typename real>
    class ODESolver
    {
    public:
        ODESolver() {};
        ODESolver(int solver_type);
        ODESolver(DiscontinuousGalerkin<dim, real> *dg_input)
        :
        dg(dg_input),
        parameters(dg->parameters)
        {};
        virtual ~ODESolver() {};
        

        virtual int steady_state () = 0;
        virtual void allocate_ode_system () = 0;
        int step_in_time();

        double residual_norm;
        unsigned int current_iteration;

    protected:
        virtual void evaluate_solution_update () = 0;
        void compute_time_step();

        Vector<real> solution;
        Vector<real> solution_update;
        Vector<real> right_hand_side;

        DiscontinuousGalerkin<dim,real> *dg;

        Parameters::AllParameters *parameters;

    }; // end of ODESolver class

    template<int dim, typename real>
    class Explicit_ODESolver
        : public ODESolver<dim, real>
    {
    public:
        Explicit_ODESolver() {};
        Explicit_ODESolver(DiscontinuousGalerkin<dim, real> *dg_input)
        :
        dg(dg_input)
        {};
        ~Explicit_ODESolver() {};
        void allocate_ode_system ();
        int steady_state ();
    protected:
        void evaluate_solution_update ();
        DiscontinuousGalerkin<dim,real> *dg;

    }; // end of Explicit_ODESolver class

    template<int dim, typename real>
    class Implicit_ODESolver
        : public ODESolver<dim, real>
    {
    public:
        Implicit_ODESolver() {};
        Implicit_ODESolver(DiscontinuousGalerkin<dim, real> *dg_input)
        :
        dg(dg_input)
        {};
        ~Implicit_ODESolver() {};
        void allocate_ode_system ();
        int steady_state ();
    protected:
        void evaluate_solution_update ();
        DiscontinuousGalerkin<dim,real> *dg;

    }; // end of Implicit_ODESolver class

    template <int dim, typename real>
    class ODESolverFactory
    {
    public:
        static ODESolver<dim,real> *create_ODESolver(DiscontinuousGalerkin<dim, real> *dg_input);
        static ODESolver<dim,real> *create_ODESolver(Parameters::ODE::SolverType solver_type);
        //static ODESolver<dim,real> *create_ODESolver(Parameters::ODE::SolverType solver_type);
    };

} // end of PHiLiP namespace

#endif
