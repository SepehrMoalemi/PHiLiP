#include <stdlib.h>     /* srand, rand */
#include <iostream>

#include <deal.II/base/convergence_table.h>

#include <deal.II/dofs/dof_tools.h>

#include <deal.II/distributed/tria.h>
#include <deal.II/grid/tria.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_refinement.h>
#include <deal.II/grid/grid_tools.h>
#include <deal.II/grid/grid_out.h>
#include <deal.II/grid/grid_in.h>

#include <deal.II/numerics/vector_tools.h>

#include <deal.II/fe/fe_values.h>

#include <Sacado.hpp>

#include "tests.h"
#include "shock_1d.h"

#include "physics/physics_factory.h"
#include "physics/manufactured_solution.h"
#include "dg/dg.h"
#include "ode_solver/ode_solver.h"


namespace PHiLiP {
namespace Tests {

/// Shocked 1D solution for 1 state variable.
/** This class also provides derivatives necessary to evaluate source terms.
 */
template <int dim, typename real>
class Shocked1D1State : public ManufacturedSolutionFunction<dim,real>
{
protected:
    using dealii::Function<dim,real>::value;
    using dealii::Function<dim,real>::gradient;
    using dealii::Function<dim,real>::hessian;
    using dealii::Function<dim,real>::vector_gradient;
public:
    /// Constructor that initializes base_values, amplitudes, frequencies.
    /** Calls the Function(const unsigned int n_components) constructor in deal.II
     *  This sets the public attribute n_components = nstate, which can then be accessed
     *  by all the other functions
     */
    Shocked1D1State (const unsigned int nstate = 1)
    : ManufacturedSolutionFunction<dim,real> (nstate)
    { };

    /// Destructor
    ~Shocked1D1State() {};
  
    /// Manufactured solution exact value
    /** 
     *  \f$ u = x \f$ when \f$ x < 0.5 \f$
     *  \f$ u = x-1 \f$ when \f$ x < 0.5 \f$
     */
    virtual real value (const dealii::Point<dim,real> &point, const unsigned int /*istate = 0*/) const override
    {
        real val = 0.0;
        return val;
        for (int d=0;d<dim;d++) {
            val += std::sin(point[d]);
        }
        //if (point[0] > 0.5) val -= 1.0;
        return val;
    };

    /// Gradient of the exact manufactured solution
    /** 
     *  \f$ \nabla u = 1 \f$
     */
    virtual dealii::Tensor<1,dim,real> gradient (const dealii::Point<dim,real> &point, const unsigned int /*istate = 0*/) const override
    {
        dealii::Tensor<1,dim,real> gradient;
        gradient = 0.0;
        return gradient;
        for (int d=0;d<dim;d++) {
            gradient[d] = std::cos(point[d]);
        }
        return gradient;
    };

    /// Hessian of the exact manufactured solution
    /** 
     *  \f$ \nabla^2 u = 0 \f$
     */
    virtual dealii::SymmetricTensor<2,dim,real> hessian (const dealii::Point<dim,real> &point, const unsigned int /*istate = 0*/) const override
    {
        dealii::SymmetricTensor<2,dim,real> hessian;
        hessian = 0;
        return hessian;
        for (int d=0;d<dim;d++) {
            hessian[d][d] = -std::sin(point[d]);
        }
        return hessian;
    }
};

template <int dim, int nstate>
double Shock1D<dim,nstate>
::integrate_solution_over_domain(DGBase<dim,double> &dg) const
{
    pcout << "Evaluating solution integral..." << std::endl;
    double solution_integral = 0.0;

    // Overintegrate the error to make sure there is not integration error in the error estimate
    //int overintegrate = 5;
    //dealii::QGauss<dim> quad_extra(dg.fe_system.tensor_degree()+overintegrate);
    //dealii::FEValues<dim,dim> fe_values_extra(dg.mapping, dg.fe_system, quad_extra, 
    //        dealii::update_values | dealii::update_JxW_values | dealii::update_quadrature_points);
    int overintegrate = 10;
    dealii::QGauss<dim> quad_extra(dg.max_degree+1+overintegrate);
    //dealii::MappingQ<dim,dim> mappingq_temp(dg.max_degree+1);
    dealii::FEValues<dim,dim> fe_values_extra(*(dg.high_order_grid.mapping_fe_field), dg.fe_collection[dg.max_degree], quad_extra, 
            dealii::update_values | dealii::update_JxW_values | dealii::update_quadrature_points);
    const unsigned int n_quad_pts = fe_values_extra.n_quadrature_points;
    std::array<double,nstate> soln_at_q;

    const bool linear_output = false;
    int power;
    if (linear_output) power = 1;
    else power = 2;

    // Integrate solution error and output error
    std::vector<dealii::types::global_dof_index> dofs_indices (fe_values_extra.dofs_per_cell);
    for (auto cell : dg.dof_handler.active_cell_iterators()) {

        if (!cell->is_locally_owned()) continue;

        fe_values_extra.reinit (cell);
        cell->get_dof_indices (dofs_indices);

        for (unsigned int iquad=0; iquad<n_quad_pts; ++iquad) {

            // Interpolate solution to quadrature points
            std::fill(soln_at_q.begin(), soln_at_q.end(), 0.0);
            for (unsigned int idof=0; idof<fe_values_extra.dofs_per_cell; ++idof) {
                const unsigned int istate = fe_values_extra.get_fe().system_to_component_index(idof).first;
                soln_at_q[istate] += dg.solution[dofs_indices[idof]] * fe_values_extra.shape_value_component(idof, iquad, istate);
            }
            // Integrate solution
            for (int s=0; s<nstate; s++) {
                solution_integral += pow(soln_at_q[0], power) * fe_values_extra.JxW(iquad);
            }
        }

    }
    const double solution_integral_mpi_sum = dealii::Utilities::MPI::sum(solution_integral, mpi_communicator);
    return solution_integral_mpi_sum;
}

template <int dim>
class SineInitialCondition : public dealii::Function<dim>
{
public:
  SineInitialCondition(const unsigned int n_components = 1,
                       const double       time         = 0.)
    : dealii::Function<dim>(n_components, time)
  {}
  virtual double value(const dealii::Point<dim> &p,
                       const unsigned int /*component*/) const override
  {
    const double PI = std::atan(1) * 4.0;
    return std::sin(p[0]*2.0*PI);
  }
};

template <int dim, int nstate>
Shock1D<dim,nstate>::Shock1D(const Parameters::AllParameters *const parameters_input)
    :
    TestsBase::TestsBase(parameters_input)
{}

template <int dim, int nstate>
void Shock1D<dim,nstate>
::initialize_perturbed_solution(DGBase<dim,double> &dg, const Physics::PhysicsBase<dim,nstate,double> &/*physics*/) const
{
    dealii::LinearAlgebra::distributed::Vector<double> solution_no_ghost;
    solution_no_ghost.reinit(dg.locally_owned_dofs, MPI_COMM_WORLD);
    dealii::VectorTools::interpolate(dg.dof_handler, SineInitialCondition<dim> (1,0), solution_no_ghost);
    dg.solution = solution_no_ghost;
}

template<int dim, int nstate>
int Shock1D<dim,nstate>
::run_test () const
{
    using ManParam = Parameters::ManufacturedConvergenceStudyParam;
    using GridEnum = ManParam::GridEnum;
    const Parameters::AllParameters param = *(TestsBase::all_parameters);

    Assert(dim == param.dimension, dealii::ExcDimensionMismatch(dim, param.dimension));
    //Assert(param.pde_type != param.PartialDifferentialEquation::euler, dealii::ExcNotImplemented());
    //if (param.pde_type == param.PartialDifferentialEquation::euler) return 1;

    ManParam manu_grid_conv_param = param.manufactured_convergence_study_param;

    const unsigned int p_start             = manu_grid_conv_param.degree_start;
    const unsigned int p_end               = manu_grid_conv_param.degree_end;

    const unsigned int n_grids_input       = manu_grid_conv_param.number_of_grids;

    // Set the physics' manufactured solution to be the Shocked1D1State manufactured solution
    using ADtype = Sacado::Fad::DFad<double>;
    using ADADtype = Sacado::Fad::DFad<ADtype>;
    std::shared_ptr <Physics::PhysicsBase<dim,nstate,double>> physics_double = Physics::PhysicsFactory<dim, nstate, double>::create_Physics(&param);
    std::shared_ptr <Physics::PhysicsBase<dim,nstate,ADtype>> physics_ADtype = Physics::PhysicsFactory<dim, nstate, ADtype>::create_Physics(&param);
    std::shared_ptr <Physics::PhysicsBase<dim,nstate,ADADtype>> physics_ADADtype = Physics::PhysicsFactory<dim, nstate, ADADtype>::create_Physics(&param);
    std::shared_ptr shocked_1d1state_double = std::make_shared < Shocked1D1State<dim,double> > (nstate);
    std::shared_ptr shocked_1d1state_ADtype = std::make_shared < Shocked1D1State<dim,ADtype> > (nstate);
    std::shared_ptr shocked_1d1state_ADADtype = std::make_shared < Shocked1D1State<dim,ADADtype> > (nstate);
    physics_double->manufactured_solution_function = shocked_1d1state_double;
    physics_ADtype->manufactured_solution_function = shocked_1d1state_ADtype;
    physics_ADADtype->manufactured_solution_function = shocked_1d1state_ADADtype;

    // Evaluate solution integral on really fine mesh
    double exact_solution_integral;
    pcout << "Evaluating EXACT solution integral..." << std::endl;
    // Limit the scope of grid_super_fine and dg_super_fine
    {
        const std::vector<int> n_1d_cells = get_number_1d_cells(n_grids_input);
#if PHILIP_DIM==1 // dealii::parallel::distributed::Triangulation<dim> does not work for 1D
        dealii::Triangulation<dim> grid_super_fine(
            typename dealii::Triangulation<dim>::MeshSmoothing(
                dealii::Triangulation<dim>::smoothing_on_refinement |
                dealii::Triangulation<dim>::smoothing_on_coarsening));
#else
        dealii::parallel::distributed::Triangulation<dim> grid_super_fine(
            this->mpi_communicator,
            typename dealii::Triangulation<dim>::MeshSmoothing(
                dealii::Triangulation<dim>::smoothing_on_refinement |
                dealii::Triangulation<dim>::smoothing_on_coarsening));
#endif
        dealii::GridGenerator::subdivided_hyper_cube(grid_super_fine, n_1d_cells[n_grids_input-1]);
        //std::shared_ptr < DGBase<dim, double> > dg_super_fine = DGFactory<dim,double>::create_discontinuous_galerkin(&param, p_end, &grid_super_fine);
        std::shared_ptr dg_super_fine = std::make_shared< DGWeak<dim,1,double> > (&param, p_end, p_end, p_end+1, &grid_super_fine);
        dg_super_fine->set_physics(physics_double);
        dg_super_fine->set_physics(physics_ADtype);
        dg_super_fine->set_physics(physics_ADADtype);
        dg_super_fine->allocate_system ();

        initialize_perturbed_solution(*dg_super_fine, *physics_double);
        exact_solution_integral = integrate_solution_over_domain(*dg_super_fine);
        pcout << "Exact solution integral is " << exact_solution_integral << std::endl;
    }

    std::vector<int> fail_conv_poly;
    std::vector<double> fail_conv_slop;
    std::vector<dealii::ConvergenceTable> convergence_table_vector;

    for (unsigned int poly_degree = p_start; poly_degree <= p_end; ++poly_degree) {

        // p0 tends to require a finer grid to reach asymptotic region
        unsigned int n_grids = n_grids_input;
        if (poly_degree <= 1) n_grids = n_grids_input + 1;

        std::vector<double> soln_error(n_grids);
        std::vector<double> output_error(n_grids);
        std::vector<double> grid_size(n_grids);

        const std::vector<int> n_1d_cells = get_number_1d_cells(n_grids);

        dealii::ConvergenceTable convergence_table;

        // Note that Triangulation must be declared before DG
        // DG will be destructed before Triangulation
        // thus removing any dependence of Triangulation and allowing Triangulation to be destructed
        // Otherwise, a Subscriptor error will occur
#if PHILIP_DIM==1 // dealii::parallel::distributed::Triangulation<dim> does not work for 1D
        dealii::Triangulation<dim> grid(
            typename dealii::Triangulation<dim>::MeshSmoothing(
                dealii::Triangulation<dim>::smoothing_on_refinement |
                dealii::Triangulation<dim>::smoothing_on_coarsening));
#else
        dealii::parallel::distributed::Triangulation<dim> grid(
            this->mpi_communicator,
            typename dealii::Triangulation<dim>::MeshSmoothing(
                dealii::Triangulation<dim>::MeshSmoothing::smoothing_on_refinement));
                //dealii::Triangulation<dim>::smoothing_on_refinement |
                //dealii::Triangulation<dim>::smoothing_on_coarsening));
#endif
        dealii::Vector<float> estimated_error_per_cell;
        for (unsigned int igrid=n_grids-1; igrid<n_grids; ++igrid) {
            grid.clear();
            dealii::GridGenerator::subdivided_hyper_cube(grid, n_1d_cells[igrid]);
            for (auto cell = grid.begin_active(); cell != grid.end(); ++cell) {
                // Set a dummy boundary ID
                cell->set_material_id(9002);
                for (unsigned int face=0; face<dealii::GeometryInfo<dim>::faces_per_cell; ++face) {
                    if (cell->face(face)->at_boundary()) cell->face(face)->set_boundary_id (1000);
                }
            }
            // Warp grid if requested in input file
            if (manu_grid_conv_param.grid_type == GridEnum::sinehypercube) dealii::GridTools::transform (&warp, grid);

            // Distort grid by random amount if requested
            const double random_factor = manu_grid_conv_param.random_distortion;
            const bool keep_boundary = true;
            if (random_factor > 0.0) dealii::GridTools::distort_random (random_factor, grid, keep_boundary);

            // Show mesh if in 2D
            //std::string gridname = "grid-"+std::to_string(igrid)+".eps";
            //if (dim == 2) print_mesh_info (grid, gridname);

            // Create DG object using the factory
            //std::shared_ptr < DGBase<dim, double> > dg = DGFactory<dim,double>::create_discontinuous_galerkin(&param, poly_degree, &grid);
            std::shared_ptr dg = std::make_shared< DGWeak<dim,1,double> > (&param, poly_degree, poly_degree, poly_degree+1, &grid);
            dg->set_physics(physics_double);
            dg->set_physics(physics_ADtype);
            dg->set_physics(physics_ADADtype);
            dg->allocate_system ();

            // Create ODE solver using the factory and providing the DG object
            std::shared_ptr<ODE::ODESolver<dim, double>> ode_solver = ODE::ODESolverFactory<dim, double>::create_ODESolver(dg);

            const unsigned int n_global_active_cells = grid.n_global_active_cells();
            const unsigned int n_dofs = dg->dof_handler.n_dofs();
            pcout << "Dimension: " << dim
                 << "\t Polynomial degree p: " << poly_degree
                 << std::endl
                 << "Grid number: " << igrid+1 << "/" << n_grids
                 << ". Number of active cells: " << n_global_active_cells
                 << ". Number of degrees of freedom: " << n_dofs
                 << std::endl;

            // Sine wave initial conditions that will form a shock.
            initialize_perturbed_solution(*(dg), *(physics_double));

            // Solve the steady state problem
            ode_solver->steady_state();

            // Overintegrate the error to make sure there is not integration error in the error estimate
            int overintegrate = 10;
            dealii::QGauss<dim> quad_extra(dg->max_degree+overintegrate);
            //dealii::MappingQ<dim,dim> mappingq(dg->max_degree+1);
            dealii::FEValues<dim,dim> fe_values_extra(*(dg->high_order_grid.mapping_fe_field), dg->fe_collection[poly_degree], quad_extra, 
                    dealii::update_values | dealii::update_JxW_values | dealii::update_quadrature_points);
            const unsigned int n_quad_pts = fe_values_extra.n_quadrature_points;
            std::array<double,nstate> soln_at_q;

            double l2error = 0;

            // Integrate solution error and output error

            std::vector<dealii::types::global_dof_index> dofs_indices (fe_values_extra.dofs_per_cell);
            estimated_error_per_cell.reinit(grid.n_active_cells());
            for (auto cell = dg->dof_handler.begin_active(); cell!=dg->dof_handler.end(); ++cell) {

                if (!cell->is_locally_owned()) continue;

                fe_values_extra.reinit (cell);
                cell->get_dof_indices (dofs_indices);

                double cell_l2error = 0;
                for (unsigned int iquad=0; iquad<n_quad_pts; ++iquad) {

                    std::fill(soln_at_q.begin(), soln_at_q.end(), 0);
                    for (unsigned int idof=0; idof<fe_values_extra.dofs_per_cell; ++idof) {
                        const unsigned int istate = fe_values_extra.get_fe().system_to_component_index(idof).first;
                        soln_at_q[istate] += dg->solution[dofs_indices[idof]] * fe_values_extra.shape_value_component(idof, iquad, istate);
                    }

                    const dealii::Point<dim> qpoint = (fe_values_extra.quadrature_point(iquad));

                    for (int istate=0; istate<nstate; ++istate) {
                        const double uexact = physics_double->manufactured_solution_function->value(qpoint, istate);
                        //l2error += pow(soln_at_q[istate] - uexact, 2) * fe_values_extra.JxW(iquad);

                        cell_l2error += pow(soln_at_q[istate] - uexact, 2) * fe_values_extra.JxW(iquad);
                    }
                }
                estimated_error_per_cell[cell->active_cell_index()] = cell_l2error;
                l2error += cell_l2error;

            }
            const double l2error_mpi_sum = std::sqrt(dealii::Utilities::MPI::sum(l2error, mpi_communicator));

            double solution_integral = integrate_solution_over_domain(*dg);

            // Convergence table
            const double dx = 1.0/pow(n_dofs,(1.0/dim));
            grid_size[igrid] = dx;
            soln_error[igrid] = l2error_mpi_sum;
            output_error[igrid] = std::abs(solution_integral - exact_solution_integral);

            convergence_table.add_value("p", poly_degree);
            convergence_table.add_value("cells", n_global_active_cells);
            convergence_table.add_value("DoFs", n_dofs);
            convergence_table.add_value("dx", dx);
            convergence_table.add_value("soln_L2_error", l2error_mpi_sum);
            convergence_table.add_value("output_error", output_error[igrid]);


            pcout << " Grid size h: " << dx 
                 << " L2-soln_error: " << l2error_mpi_sum
                 << " Residual: " << ode_solver->residual_norm
                 << std::endl;

            pcout << " output_exact: " << exact_solution_integral
                 << " output_discrete: " << solution_integral
                 << " output_error: " << output_error[igrid]
                 << std::endl;

            if (igrid > 0) {
                const double slope_soln_err = log(soln_error[igrid]/soln_error[igrid-1])
                                      / log(grid_size[igrid]/grid_size[igrid-1]);
                const double slope_output_err = log(output_error[igrid]/output_error[igrid-1])
                                      / log(grid_size[igrid]/grid_size[igrid-1]);
                pcout << "From grid " << igrid-1
                     << "  to grid " << igrid
                     << "  dimension: " << dim
                     << "  polynomial degree p: " << poly_degree
                     << std::endl
                     << "  solution_error1 " << soln_error[igrid-1]
                     << "  solution_error2 " << soln_error[igrid]
                     << "  slope " << slope_soln_err
                     << std::endl
                     << "  solution_integral_error1 " << output_error[igrid-1]
                     << "  solution_integral_error2 " << output_error[igrid]
                     << "  slope " << slope_output_err
                     << std::endl;
            }

        }
        pcout << " ********************************************"
             << std::endl
             << " Convergence rates for p = " << poly_degree
             << std::endl
             << " ********************************************"
             << std::endl;
        convergence_table.evaluate_convergence_rates("soln_L2_error", "cells", dealii::ConvergenceTable::reduction_rate_log2, dim);
        convergence_table.evaluate_convergence_rates("output_error", "cells", dealii::ConvergenceTable::reduction_rate_log2, dim);
        convergence_table.set_scientific("dx", true);
        convergence_table.set_scientific("soln_L2_error", true);
        convergence_table.set_scientific("output_error", true);
        if (pcout.is_active()) convergence_table.write_text(pcout.get_stream());

        convergence_table_vector.push_back(convergence_table);

        const double expected_slope = poly_degree+1;

        const double last_slope = log(soln_error[n_grids-1]/soln_error[n_grids-2])
                                  / log(grid_size[n_grids-1]/grid_size[n_grids-2]);
        double before_last_slope = last_slope;
        if ( n_grids > 2 ) {
        before_last_slope = log(soln_error[n_grids-2]/soln_error[n_grids-3])
                            / log(grid_size[n_grids-2]/grid_size[n_grids-3]);
        }
        const double slope_avg = 0.5*(before_last_slope+last_slope);
        const double slope_diff = slope_avg-expected_slope;

        double slope_deficit_tolerance = -std::abs(manu_grid_conv_param.slope_deficit_tolerance);
        if(poly_degree == 0) slope_deficit_tolerance *= 2; // Otherwise, grid sizes need to be much bigger for p=0

        if (slope_diff < slope_deficit_tolerance) {
            pcout << std::endl
                 << "Convergence order not achieved. Average last 2 slopes of "
                 << slope_avg << " instead of expected "
                 << expected_slope << " within a tolerance of "
                 << slope_deficit_tolerance
                 << std::endl;
            // p=0 just requires too many meshes to get into the asymptotic region.
            if(poly_degree!=0) fail_conv_poly.push_back(poly_degree);
            if(poly_degree!=0) fail_conv_slop.push_back(slope_avg);
        }

    }
    pcout << std::endl << std::endl << std::endl << std::endl;
    pcout << " ********************************************" << std::endl;
    pcout << " Convergence summary" << std::endl;
    pcout << " ********************************************" << std::endl;
    for (auto conv = convergence_table_vector.begin(); conv!=convergence_table_vector.end(); conv++) {
        if (pcout.is_active()) conv->write_text(pcout.get_stream());
        pcout << " ********************************************" << std::endl;
    }
    int n_fail_poly = fail_conv_poly.size();
    if (n_fail_poly > 0) {
        for (int ifail=0; ifail < n_fail_poly; ++ifail) {
            const double expected_slope = fail_conv_poly[ifail]+1;
            const double slope_deficit_tolerance = -std::abs(manu_grid_conv_param.slope_deficit_tolerance);
            pcout << std::endl
                 << "Convergence order not achieved for polynomial p = "
                 << fail_conv_poly[ifail]
                 << ". Slope of "
                 << fail_conv_slop[ifail] << " instead of expected "
                 << expected_slope << " within a tolerance of "
                 << slope_deficit_tolerance
                 << std::endl;
        }
    }
    return n_fail_poly;
}

template <int dim, int nstate>
dealii::Point<dim> Shock1D<dim,nstate>
::warp (const dealii::Point<dim> &p)
{
    dealii::Point<dim> q = p;
    q[dim-1] *= 1.5;
    if (dim >= 2) q[0] += 1*std::sin(q[dim-1]);
    if (dim >= 3) q[1] += 1*std::cos(q[dim-1]);
    return q;
}

template <int dim, int nstate>
void Shock1D<dim,nstate>
::print_mesh_info(const dealii::Triangulation<dim> &triangulation, const std::string &filename) const
{
    pcout << "Mesh info:" << std::endl
         << " dimension: " << dim << std::endl
         << " no. of cells: " << triangulation.n_global_active_cells() << std::endl;
    std::map<dealii::types::boundary_id, unsigned int> boundary_count;
    for (auto cell : triangulation.active_cell_iterators()) {
        for (unsigned int face=0; face < dealii::GeometryInfo<dim>::faces_per_cell; ++face) {
            if (cell->face(face)->at_boundary()) boundary_count[cell->face(face)->boundary_id()]++;
        }
    }
    pcout << " boundary indicators: ";
    for (const std::pair<const dealii::types::boundary_id, unsigned int> &pair : boundary_count) {
        pcout      << pair.first << "(" << pair.second << " times) ";
    }
    pcout << std::endl;
    if (dim == 2) {
        std::ofstream out (filename);
        dealii::GridOut grid_out;
        grid_out.write_eps (triangulation, out);
        pcout << " written to " << filename << std::endl << std::endl;
    }
}

template class Shock1D <PHILIP_DIM,1>;

} // Tests namespace
} // PHiLiP namespace

