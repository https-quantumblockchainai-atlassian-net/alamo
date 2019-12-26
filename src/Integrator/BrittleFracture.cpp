#include "BrittleFracture.H"

namespace Integrator
{
BrittleFracture::BrittleFracture() :
	Integrator()
{
	// Crack model
	amrex::ParmParse pp_crack("crack");
	std::string crack_type;
	pp_crack.query("type",crack_type);
	pp_crack.query("modulus_scaling_max",scaleModulusMax);
	pp_crack.query("refinement_threshold",refinement_threshold);

	if(crack_type=="constant")
		boundary = new Model::Interface::Crack::Constant();
	else if(crack_type == "sin")
		boundary = new Model::Interface::Crack::Sin();
	else
		Util::Abort(INFO,"This crack model hasn't been implemented yet");

	// ICs
	amrex::ParmParse pp("ic"); // Phase-field model parameters
	pp.query("type", ic_type);

	if(ic_type == "ellipsoid")
		ic = new IC::Ellipsoid(geom);
	else if(ic_type == "notch")
		ic = new IC::Notch(geom);
	else
		Util::Abort(INFO,"This type of IC hasn't been implemented yet");
	
	pp_crack.query("tol_crack",tol_crack);
	pp_crack.query("tol_step",tol_step);
	// BCs
	// Crack field should have a zero neumann BC. So we just code it up here.
	// In case this needs to change, we can add options to read it from input.
	amrex::Vector<std::string> bc_hi_str(AMREX_SPACEDIM), bc_lo_str(AMREX_SPACEDIM);
	amrex::Vector<Set::Scalar> AMREX_D_DECL(bc_lo_1,bc_lo_2,bc_lo_3);
	amrex::Vector<Set::Scalar> AMREX_D_DECL(bc_hi_1,bc_hi_2,bc_hi_3);

	// Below are conditions for full simulation.  If this doesn't work, we can
	// try symmetric simulation.
	bc_lo_str = {AMREX_D_DECL("Neumann", "Neumann", "Neumann")};
	bc_hi_str = {AMREX_D_DECL("Neumann", "Neumann", "Neumann")};
	//bc_lo_str = {AMREX_D_DECL("Dirichlet", "Dirichlet", "Dirichlet")};
	//bc_hi_str = {AMREX_D_DECL("Dirichlet", "Dirichlet", "Dirichlet")};
	/*/bc_lo_str = {AMREX_D_DECL("REFLECT_EVEN", "REFLECT_EVEN", "REFLECT_EVEN")};
	bc_hi_str = {AMREX_D_DECL("Neumann", "Neumann", "Neumann")};*/
	AMREX_D_TERM( 	bc_lo_1 = {0.}; bc_hi_1 = {0.};,
					bc_lo_2 = {0.}; bc_hi_2 = {0.};,
					bc_lo_3 = {0.}; bc_hi_3 = {0.};
	);
	mybc = new BC::Constant(bc_hi_str, bc_lo_str
				  ,AMREX_D_DECL(bc_lo_1, bc_lo_2, bc_lo_3)
				  ,AMREX_D_DECL(bc_hi_1, bc_hi_2, bc_hi_3));

	RegisterNewFab(m_c,     mybc, 1, number_of_ghost_cells+1, "c",		true);
	RegisterNewFab(m_c_old, mybc, 1, number_of_ghost_cells+1, "c_old",	true);
	RegisterNewFab(m_driving_force, mybc, 4, number_of_ghost_cells+1, "driving_force",true);
	
	crack_err_norm = 0.; crack_err_temp_norm = 0.;
	crack_err_norm_init = 1.e4; crack_err_temp_norm_init = 1.e4;

	disp_err_norm = 0.; disp_err_norm_init = 1.e4;

	RegisterIntegratedVariable(&crack_err_norm, "crack_err_norm");
	RegisterIntegratedVariable(&c_new_norm,"c_new_norm");
	
	// Material input
	amrex::ParmParse pp_material("material");
	pp_material.query("model",input_material);
	if(input_material == "isotropic")
	{
		//Util::Abort(INFO, "Check for model_type in PD.H");
		Set::Scalar lambda = 410.0;
		Set::Scalar mu = 305.0;
		amrex::ParmParse pp_material_isotropic("material.isotropic");
		pp_material_isotropic.query("lambda",lambda);
		pp_material_isotropic.query("mu",mu);
		if(lambda <=0) { Util::Warning(INFO,"Lambda must be positive. Resetting back to default value"); lambda = 410.0; }
		if(mu <= 0) { Util::Warning(INFO,"Mu must be positive. Resetting back to default value"); mu = 305.0; }
		modeltype = new fracture_model_type(lambda,mu);
		//Util::Message(INFO, "lambda = ", modeltype->GetLambda(), " mu = ", modeltype->GetMu());
	}
	else
		Util::Abort(INFO,"This model has not been implemented yet.");

	// Crack properties
	
	//pp_crack.query("C_phi",crack.C_phi);

	// Elasticity properties
	amrex::ParmParse pp_elastic("elastic");
	pp_elastic.query("int",				elastic.interval);
	pp_elastic.query("type",			elastic.type);
	pp_elastic.query("max_iter",		elastic.max_iter);
	pp_elastic.query("max_fmg_iter",	elastic.max_fmg_iter);
	pp_elastic.query("verbose",			elastic.verbose);
	pp_elastic.query("cgverbose",		elastic.cgverbose);
	pp_elastic.query("tol_rel",			elastic.tol_rel);
	pp_elastic.query("tol_abs",			elastic.tol_abs);
	pp_elastic.query("cg_tol_rel",		elastic.cg_tol_rel);
	pp_elastic.query("cg_tol_abs",		elastic.cg_tol_abs);
	pp_elastic.query("use_fsmooth",		elastic.use_fsmooth);
	pp_elastic.query("agglomeration", 	elastic.agglomeration);
	pp_elastic.query("consolidation", 	elastic.consolidation);

	pp_elastic.query("bottom_solver",elastic.bottom_solver);
	pp_elastic.query("linop_maxorder", elastic.linop_maxorder);
	pp_elastic.query("max_coarsening_level",elastic.max_coarsening_level);
	pp_elastic.query("verbose",elastic.verbose);
	pp_elastic.query("cg_verbose", elastic.cgverbose);
	pp_elastic.query("bottom_max_iter", elastic.bottom_max_iter);
	pp_elastic.query("max_fixed_iter", elastic.max_fixed_iter);
	pp_elastic.query("bottom_tol", elastic.bottom_tol);

	if (pp_elastic.countval("body_force")) pp_elastic.getarr("body_force",elastic.body_force);

	amrex::ParmParse pp_elastic_bc("elastic.bc");
	amrex::Vector<std::string> AMREX_D_DECL(bc_x_lo_str,bc_y_lo_str,bc_z_lo_str);
	amrex::Vector<std::string> AMREX_D_DECL(bc_x_hi_str,bc_y_hi_str,bc_z_hi_str);

	std::map<std::string,BC::Operator::Elastic<fracture_model_type>::Type >        bc_map;
	bc_map["displacement"] 	= BC::Operator::Elastic<fracture_model_type>::Type::Displacement;
	bc_map["disp"] 			= BC::Operator::Elastic<fracture_model_type>::Type::Displacement;
	bc_map["traction"] 		= BC::Operator::Elastic<fracture_model_type>::Type::Traction;
	bc_map["trac"] 			= BC::Operator::Elastic<fracture_model_type>::Type::Traction;
	bc_map["neumann"] 		= BC::Operator::Elastic<fracture_model_type>::Type::Neumann;
	bc_map["periodic"] 		= BC::Operator::Elastic<fracture_model_type>::Type::Periodic;

		
	AMREX_D_TERM(	bc_lo_1.clear(); bc_hi_1.clear();,
					bc_lo_2.clear(); bc_hi_2.clear();,
					bc_lo_3.clear(); bc_hi_3.clear(););
	//amrex::Vector<Set::Scalar> bc_lo_1, bc_hi_1;
	//amrex::Vector<Set::Scalar> bc_lo_2, bc_hi_2;
	//amrex::Vector<Set::Scalar> bc_lo_3, bc_hi_3;

	/* Need to replace this later with a proper specification of boundary.
	   Right now we are hard-coding the tensile test and just requesting
	   rate of pulling. */
	pp_elastic_bc.query("disp_step",elastic.test_rate);
	pp_elastic_bc.query("disp_init",elastic.test_init);
	pp_elastic_bc.query("max_disp",elastic.test_max);
	pp_elastic_bc.query("crackStressTest",crackStressTest);

	elastic.bc_top[1] = elastic.test_init;

	if(elastic.test_rate < 0.) { Util::Warning(INFO,"Rate can't be less than zero. Resetting to 1.0"); elastic.test_rate = 0.1; }
	if(elastic.test_max < 0. ||  elastic.test_max < elastic.test_rate) {Util::Warning(INFO,"Max can't be less than load step. Resetting to load step"); elastic.test_max = elastic.test_rate;}

	//Below are the conditions for full tensile test simulation. 
	// If this doesn't work we can try symmetric simulation
	AMREX_D_TERM( 	bc_x_lo_str = {AMREX_D_DECL("trac", "trac", "trac")};
					bc_x_hi_str = {AMREX_D_DECL("trac", "trac", "trac")};
					,
					//bc_y_lo_str = {AMREX_D_DECL("disp", "disp", "disp")};
					//bc_y_hi_str = {AMREX_D_DECL("trac", "trac", "trac")};
					bc_y_lo_str = {AMREX_D_DECL("trac", "disp", "trac")};
					bc_y_hi_str = {AMREX_D_DECL("trac", "disp", "trac")};
					,
					bc_z_lo_str = {AMREX_D_DECL("trac", "trac", "trac")};
					bc_z_hi_str = {AMREX_D_DECL("trac", "trac", "trac")};);

	/*AMREX_D_TERM( 	bc_x_lo_str = {AMREX_D_DECL("disp", "neumann", "neumann")};
					bc_x_hi_str = {AMREX_D_DECL("disp", "trac", "trac")};
					,
					bc_y_lo_str = {AMREX_D_DECL("neumann", "disp", "neumann")};
					bc_y_hi_str = {AMREX_D_DECL("trac", "trac", "trac")};
					,
					bc_z_lo_str = {AMREX_D_DECL("neumann", "neumann", "disp")};
					bc_z_hi_str = {AMREX_D_DECL("trac", "trac", "trac")};);*/

	AMREX_D_TERM(	elastic.bc_xlo = {AMREX_D_DECL(bc_map[bc_x_lo_str[0]],bc_map[bc_x_lo_str[1]],bc_map[bc_x_lo_str[2]])};
					elastic.bc_xhi = {AMREX_D_DECL(bc_map[bc_x_hi_str[0]],bc_map[bc_x_hi_str[1]],bc_map[bc_x_hi_str[2]])};
					,
					elastic.bc_ylo = {AMREX_D_DECL(bc_map[bc_y_lo_str[0]],bc_map[bc_y_lo_str[1]],bc_map[bc_y_lo_str[2]])};
					elastic.bc_yhi = {AMREX_D_DECL(bc_map[bc_y_hi_str[0]],bc_map[bc_y_hi_str[1]],bc_map[bc_y_hi_str[2]])};
					,
					elastic.bc_zlo = {AMREX_D_DECL(bc_map[bc_z_lo_str[0]],bc_map[bc_z_lo_str[1]],bc_map[bc_z_lo_str[2]])};
					elastic.bc_zhi = {AMREX_D_DECL(bc_map[bc_z_hi_str[0]],bc_map[bc_z_hi_str[1]],bc_map[bc_z_hi_str[2]])};);

	AMREX_D_TERM(	elastic.bc_left = Set::Vector(AMREX_D_DECL(0.,0.,0.));
					elastic.bc_right = Set::Vector(AMREX_D_DECL(0.,0.,0.));
					,
					elastic.bc_bottom = Set::Vector(AMREX_D_DECL(0.,0.,0.));
					elastic.bc_top = Set::Vector(AMREX_D_DECL(0.,0.,0.));
					,
					elastic.bc_back = Set::Vector(AMREX_D_DECL(0.,0.,0.));
					elastic.bc_front = Set::Vector(AMREX_D_DECL(0.,0.,0.)););
	
	const int number_of_stress_components = AMREX_SPACEDIM*AMREX_SPACEDIM;
	
	RegisterNodalFab (m_disp, 		AMREX_SPACEDIM, 				number_of_ghost_cells, "Disp",true);
	RegisterNodalFab (m_rhs,  		AMREX_SPACEDIM, 				number_of_ghost_cells, "RHS",true);
	RegisterNodalFab (m_strain,		number_of_stress_components,	number_of_ghost_cells,	"strain",true);
	RegisterNodalFab (m_stress,		number_of_stress_components,	number_of_ghost_cells,	"stress",true);
	RegisterNodalFab (m_stressvm,	1,								number_of_ghost_cells,	"stress_vm",true);
	RegisterNodalFab (m_energy,		1,								number_of_ghost_cells,	"energy",true);
	RegisterNodalFab (m_energy_pristine,		1,					number_of_ghost_cells,	"energyP",true);
	RegisterNodalFab (m_energy_pristine_old,	1,					number_of_ghost_cells,	"energyPOld",true);
	RegisterNodalFab (m_residual,	AMREX_SPACEDIM,					number_of_ghost_cells,	"residual",true);
	nlevels = maxLevel() + 1;
}

BrittleFracture::~BrittleFracture()
{
}

void
BrittleFracture::Initialize (int lev)
{
	Util::Message(INFO);
	ic->Initialize(lev,m_c);
	ic->Initialize(lev,m_c_old);
	m_driving_force[lev]->setVal(0.0);
	
	Util::Message(INFO);
	m_disp[lev]->setVal(0.0);
	m_strain[lev]->setVal(0.0);
	m_stress[lev]->setVal(0.0);
	m_stressvm[lev]->setVal(0.0);
	m_rhs[lev]->setVal(0.0);
	m_energy[lev]->setVal(0.0);
	m_residual[lev]->setVal(0.0);
	m_energy_pristine[lev] -> setVal(0.);
	m_energy_pristine_old[lev] -> setVal(0.);
	Util::Message(INFO);
}

void
BrittleFracture::ScaledModulus(int lev, amrex::FabArray<amrex::BaseFab<fracture_model_type> > &model)
{
	/*
	  This function is supposed to degrade material parameters based on certain
	  fracture model.
	  For now we are just using isotropic degradation.
	*/
	Util::Message(INFO);
	static amrex::IntVect AMREX_D_DECL(dx(AMREX_D_DECL(1,0,0)),
					   dy(AMREX_D_DECL(0,1,0)),
					   dz(AMREX_D_DECL(0,0,1)));
	m_c[lev]->FillBoundary();

	for (amrex::MFIter mfi(model,true); mfi.isValid(); ++mfi)
	{
		amrex::Box box = mfi.growntilebox(2);
		amrex::Array4<const amrex::Real> const& c_new = (*m_c[lev]).array(mfi);
		amrex::Array4<fracture_model_type> const& modelfab = model.array(mfi);

		amrex::ParallelFor (box,[=] AMREX_GPU_DEVICE(int i, int j, int k){
			Set::Scalar mul = AMREX_D_PICK(0.5,0.25,0.125);
			amrex::Vector<Set::Scalar> _temp;
			_temp.push_back( mul*(AMREX_D_TERM(	
								boundary->g_phi(c_new(i,j,k,0),0.) + boundary->g_phi(c_new(i-1,j,k,0),0.)
								, 
								+ boundary->g_phi(c_new(i,j-1,k,0),0.) + boundary->g_phi(c_new(i-1,j-1,k,0),0.)
								, 
								+ boundary->g_phi(c_new(i,j,k-1,0),0.) + boundary->g_phi(c_new(i-1,j,k-1,0),0.)
								+ boundary->g_phi(c_new(i,j-1,k-1,0),0.) + boundary->g_phi(c_new(i-1,j-1,k-1,0),0.))
								));
			//Util::Message(INFO, "c = ", c_new(i,j,k,0) ," temp = ", _temp[0], " ScaleModulusMax = ", scaleModulusMax);
			if(_temp[0] < 0.0) _temp[0] = 0.;
			if(_temp[0] > 1.0) _temp[0] = 1.0;
			//Util::Message(INFO, modelfab(i,j,k,0).GetMu(), " ", modelfab(i,j,k,0).GetLambda());
			modelfab(i,j,k,0).DegradeModulus(std::min(1.-_temp[0],1.-scaleModulusMax));
			//Util::Message(INFO, modelfab(i,j,k,0).GetMu(), " ", modelfab(i,j,k,0).GetLambda());
		});
	}
	amrex::Geometry tmp_geom = geom[lev];
    for (int i = 0; i < 2; i++)
    {
		Util::Message(INFO);
    	amrex::FabArray<amrex::BaseFab<fracture_model_type>> &mf = model;
        mf.FillBoundary(tmp_geom.periodicity());
        mf.FillBoundary();
        const int ncomp = mf.nComp();
        const int ng1 = 1;
        const int ng2 = 2;
        amrex::FabArray<amrex::BaseFab<fracture_model_type>> tmpmf(mf.boxArray(), mf.DistributionMap(), ncomp, ng1);
        amrex::Copy(tmpmf, mf, 0, 0, ncomp, ng1);
        mf.ParallelCopy(tmpmf, 0, 0, ncomp, ng1, ng2, tmp_geom.periodicity());
    }

}

void 
BrittleFracture::TimeStepBegin(amrex::Real /*time*/, int /*iter*/)
{
	Util::Message(INFO);
	if(crackStressTest)
	{
		Util::Message(INFO);
		elastic.bc_top[1] = elastic.test_init + ((double)elastic.test_step)*elastic.test_rate;
		ElasticityProblem(0.);
		solveElasticity = false;
		return;
	}
	if(newCrackProblem)
	{
		Util::Message(INFO);
		elastic.bc_top[1] = elastic.test_init + ((double)elastic.test_step)*elastic.test_rate;
		newCrackProblem = false;
	}
	ElasticityProblem(0.);	
}


void
BrittleFracture::Advance (int lev, amrex::Real /*time*/, amrex::Real dt)
{
	if(crackStressTest) return;
	CrackProblem(lev,0.,dt);
}

void
BrittleFracture::TimeStepComplete(amrex::Real time,int iter)
{
	if(crackStressTest) 
	{
		amrex::Vector<Set::Scalar> plottime;
		amrex::Vector<int> plotstep;
		std::string plotfolder = "crack";

		plottime.resize(nlevels);
		plotstep.resize(nlevels);
		for (int lev = 0; lev < nlevels; lev++) {plottime[lev] = (double)elastic.test_step; plotstep[lev]=elastic.test_step;}
		WritePlotFile(plotfolder,plottime,plotstep);

		SetStopTime(time-0.01);return;
	}
	IntegrateVariables(time,iter);
	
	Util::Message(INFO, "crack_err_norm = ", crack_err_norm);
	Util::Message(INFO, "c_new_norm = ", c_new_norm);
	Util::Message(INFO, "relative error = ", crack_err_norm/c_new_norm);
	
	if(crack_err_norm/c_new_norm > tol_crack) return;

	crack_err_norm = 0.; c_new_norm = 0.;
	
	amrex::Vector<Set::Scalar> plottime;
	amrex::Vector<int> plotstep;
	std::string plotfolder = "crack";

	plottime.resize(nlevels);
	plotstep.resize(nlevels);
	for (int lev = 0; lev < nlevels; lev++) {plottime[lev] = (double)elastic.test_step; plotstep[lev]=elastic.test_step;}
	WritePlotFile(plotfolder,plottime,plotstep);

	newCrackProblem = true;
	elastic.test_step++;
	if(elastic.bc_top[1] >= elastic.test_max) SetStopTime(time-0.01);
	
	crack_err_norm = 0.; c_new_norm = 0.;
}

void
BrittleFracture::CrackProblem(int lev, amrex::Real /*time*/, amrex::Real dt)
{
	std::swap(*m_c_old[lev], *m_c[lev]);

	static amrex::IntVect AMREX_D_DECL(	dx(AMREX_D_DECL(1,0,0)), dy(AMREX_D_DECL(0,1,0)), dz(AMREX_D_DECL(0,0,1)));
	const amrex::Real* DX = geom[lev].CellSize();

	for ( amrex::MFIter mfi(*m_c[lev],true); mfi.isValid(); ++mfi )
	{
		const amrex::Box& bx = mfi.validbox();
		amrex::Array4<amrex::Real> const& c_old = (*m_c_old[lev]).array(mfi);
		amrex::Array4<amrex::Real> const& df = (*m_driving_force[lev]).array(mfi);
		amrex::Array4<amrex::Real> const& c_new = (*m_c[lev]).array(mfi);
		amrex::Array4<const Set::Scalar> const& energy_box = (*m_energy_pristine[lev]).array(mfi);

		amrex::ParallelFor (bx,[=] AMREX_GPU_DEVICE(int i, int j, int k){
			Set::Scalar laplacian = Numeric::Laplacian(c_old,i,j,k,0,DX);

			Set::Vector Dcold = Numeric::Gradient(c_old, i, j, k, 0, DX);
			//Set::Scalar normgrad = Dcold.lpNorm<2>();
			//Set::Vector normal = Dcold / normgrad;
			Set::Scalar Theta = atan2(Dcold(1),Dcold(0));
			
			Set::Scalar rhs = 0.;	
			Set::Scalar en_cell = Numeric::Interpolate::NodeToCellAverage(energy_box,i,j,k,0);
			df(i,j,k,0) = boundary->Dg_phi(c_old(i,j,k,0),0.)*en_cell;
			df(i,j,k,1) = boundary->Epc(Theta)*boundary->Dw_phi(c_old(i,j,k,0),0.);
			df(i,j,k,2) = boundary->kappa(Theta)*laplacian;

			rhs += boundary->Dg_phi(c_old(i,j,k,0),0.)*en_cell;
			rhs += boundary->Epc(Theta)*boundary->Dw_phi(c_old(i,j,k,0),0.);
			rhs -= boundary->kappa(Theta)*laplacian;

			df(i,j,k,3) = std::max(0.,rhs);
			
			if(std::isnan(rhs)) Util::Abort(INFO, "Dwphi = ", boundary->Dw_phi(c_old(i,j,k,0),0.),". c_old(i,j,k,0) = ",c_old(i,j,k,0));
			c_new(i,j,k,0) = c_old(i,j,k,0) - dt*std::max(0.,rhs)*boundary->GetMobility(c_old(i,j,k,0));

			if(c_new(i,j,k,0) > 1.0) {Util::Message(INFO, "cnew = ", c_new(i,j,k,0) ,", resetting to 1.0"); c_new(i,j,k,0) = 1.;}
			if(c_new(i,j,k,0) < 0.0) {Util::Message(INFO, "cnew = ", c_new(i,j,k,0) ,", resetting to 0.0"); c_new(i,j,k,0) = 0.;}
		});
	}
}

void
BrittleFracture::ElasticityProblem(amrex::Real /*time*/)
{
	LPInfo info;
	info.setAgglomeration(elastic.agglomeration);
	info.setConsolidation(elastic.consolidation);
	info.setMaxCoarseningLevel(elastic.max_coarsening_level);

	amrex::Vector<amrex::FabArray<amrex::BaseFab<fracture_model_type> > > model;
	model.resize(nlevels);
	for (int ilev = 0; ilev < nlevels; ++ilev)
	{
		model[ilev].define(m_disp[ilev]->boxArray(), m_disp[ilev]->DistributionMap(), 1, number_of_ghost_cells);
		model[ilev].setVal(*modeltype);
		std::swap(*m_energy_pristine_old[ilev], *m_energy_pristine[ilev]);
		m_energy_pristine[ilev]->setVal(0.);
		ScaledModulus(ilev,model[ilev]);
	}

	Util::Message(INFO);
	Operator::Elastic<fracture_model_type> elastic_operator;
	Util::Message(INFO);
	elastic_operator.define(geom, grids, dmap, info);
	Util::Message(INFO);

	for (int ilev = 0; ilev < nlevels; ++ilev)
		elastic_operator.SetModel(ilev,model[ilev]);
	Util::Message(INFO);
	
	elastic_operator.setMaxOrder(elastic.linop_maxorder);
	Util::Message(INFO);
	BC::Operator::Elastic<fracture_model_type> bc;
	Util::Message(INFO);
	
	Util::Message(INFO);
	for (int ilev = 0; ilev < nlevels; ++ilev)
	{
		const Real* DX = geom[ilev].CellSize();
		Set::Scalar volume = AMREX_D_TERM(DX[0],*DX[1],*DX[2]);

		AMREX_D_TERM(m_rhs[ilev]->setVal(elastic.body_force[0]*volume,0,1);,
			     m_rhs[ilev]->setVal(elastic.body_force[1]*volume,1,1);,
			     m_rhs[ilev]->setVal(elastic.body_force[2]*volume,2,1););
	}
	
	AMREX_D_TERM(
		bc.Set(bc.Face::XLO, bc.Direction::X, elastic.bc_xlo[0], elastic.bc_left[0], 	m_rhs, geom);
		bc.Set(bc.Face::XHI, bc.Direction::X, elastic.bc_xhi[0], elastic.bc_right[0], 	m_rhs, geom);
		,
		bc.Set(bc.Face::XLO, bc.Direction::Y, elastic.bc_xlo[1], elastic.bc_left[1], 	m_rhs, geom);
		bc.Set(bc.Face::XHI, bc.Direction::Y, elastic.bc_xhi[1], elastic.bc_right[1], 	m_rhs, geom);
		bc.Set(bc.Face::YLO, bc.Direction::X, elastic.bc_ylo[0], elastic.bc_bottom[0], 	m_rhs, geom);
		bc.Set(bc.Face::YLO, bc.Direction::Y, elastic.bc_ylo[1], elastic.bc_bottom[1], 	m_rhs, geom);
		bc.Set(bc.Face::YHI, bc.Direction::X, elastic.bc_yhi[0], elastic.bc_top[0], 	m_rhs, geom);
		bc.Set(bc.Face::YHI, bc.Direction::Y, elastic.bc_yhi[1], elastic.bc_top[1], 	m_rhs, geom);
		,
		bc.Set(bc.Face::XLO, bc.Direction::Z, elastic.bc_xlo[2], elastic.bc_left[2], 	m_rhs, geom);
		bc.Set(bc.Face::XHI, bc.Direction::Z, elastic.bc_xhi[2], elastic.bc_right[2], 	m_rhs, geom);
		bc.Set(bc.Face::YLO, bc.Direction::Z, elastic.bc_ylo[2], elastic.bc_bottom[2], 	m_rhs, geom);
		bc.Set(bc.Face::YHI, bc.Direction::Z, elastic.bc_yhi[2], elastic.bc_top[2], 	m_rhs, geom);
		bc.Set(bc.Face::ZLO, bc.Direction::X, elastic.bc_zlo[0], elastic.bc_back[0], 	m_rhs, geom);
		bc.Set(bc.Face::ZLO, bc.Direction::Y, elastic.bc_zlo[1], elastic.bc_back[1], 	m_rhs, geom);
		bc.Set(bc.Face::ZLO, bc.Direction::Z, elastic.bc_zlo[2], elastic.bc_back[2], 	m_rhs, geom);
		bc.Set(bc.Face::ZHI, bc.Direction::X, elastic.bc_zhi[0], elastic.bc_front[0], 	m_rhs, geom);
		bc.Set(bc.Face::ZHI, bc.Direction::Y, elastic.bc_zhi[1], elastic.bc_front[1], 	m_rhs, geom);
		bc.Set(bc.Face::ZHI, bc.Direction::Z, elastic.bc_zhi[2], elastic.bc_front[2], 	m_rhs, geom);
	);

	elastic_operator.SetBC(&bc);
	Solver::Nonlocal::Linear solver(elastic_operator);
	solver.setMaxIter(elastic.max_iter);
	solver.setMaxFmgIter(elastic.max_fmg_iter);
	solver.setFixedIter(elastic.max_fixed_iter);
	solver.setVerbose(elastic.verbose);
	solver.setCGVerbose(elastic.cgverbose);
	solver.setBottomMaxIter(elastic.bottom_max_iter);
	solver.setBottomTolerance(elastic.cg_tol_rel) ;
	solver.setBottomToleranceAbs(elastic.cg_tol_abs) ;

	for (int ilev = 0; ilev < nlevels; ilev++) if (m_disp[ilev]->contains_nan()) Util::Warning(INFO);

	if (elastic.bottom_solver == "cg") solver.setBottomSolver(MLMG::BottomSolver::cg);
	else if (elastic.bottom_solver == "bicgstab") solver.setBottomSolver(MLMG::BottomSolver::bicgstab);

	/*amrex::Vector<Set::Scalar> plottime;
	amrex::Vector<int> plotstep;
	std::string plotfolder = "crack";

	plottime.resize(nlevels);
	plotstep.resize(nlevels);
	for (int lev = 0; lev < nlevels; lev++) {plottime[lev] = (double)elastic.test_step; plotstep[lev]=elastic.test_step;}
	WritePlotFile(plotfolder,plottime,plotstep);*/

	solver.solve(GetVecOfPtrs(m_disp), GetVecOfConstPtrs(m_rhs), elastic.tol_rel, elastic.tol_abs);
	solver.compResidual(GetVecOfPtrs(m_residual),GetVecOfPtrs(m_disp),GetVecOfConstPtrs(m_rhs));
	//solver.solve(GetVecOfPtrs(m_disp), GetVecOfConstPtrs(m_rhs), elastic.tol_rel, elastic.tol_abs);

	for (int lev = 0; lev < nlevels; lev++)
	{
		elastic_operator.Strain(lev,*m_strain[lev],*m_disp[lev]);
		elastic_operator.Stress(lev,*m_stress[lev],*m_disp[lev]);
		elastic_operator.Energy(lev,*m_energy[lev],*m_disp[lev]);
	}
	for (int lev = 0; lev < nlevels; lev++)
	{
		for (amrex::MFIter mfi(*m_strain[lev],true); mfi.isValid(); ++mfi)
		{
			const amrex::Box& box = mfi.validbox();
			amrex::Array4<const Set::Scalar> const& strain_box = (*m_strain[lev]).array(mfi);
			amrex::Array4<Set::Scalar> const& energy_box = (*m_energy_pristine[lev]).array(mfi);
			amrex::ParallelFor (box,[=] AMREX_GPU_DEVICE(int i, int j, int k){
				//energy_box(i,j,k,0) = 0.;
				Set::Matrix eps;
				AMREX_D_PICK(
					eps(0,0) = strain_box(i,j,k,0);
					,
					eps(0,0) = strain_box(i,j,k,0); eps(0,1) = strain_box(i,j,k,1);
					eps(1,0) = strain_box(i,j,k,2); eps(1,1) = strain_box(i,j,k,3);
					,
					eps(0,0) = strain_box(i,j,k,0); eps(0,1) = strain_box(i,j,k,1); eps(0,2) = strain_box(i,j,k,2);
					eps(1,0) = strain_box(i,j,k,3); eps(1,1) = strain_box(i,j,k,4); eps(1,2) = strain_box(i,j,k,5);
					eps(2,0) = strain_box(i,j,k,6); eps(2,1) = strain_box(i,j,k,7); eps(2,2) = strain_box(i,j,k,8);
				);
				Set::Matrix sig;
				sig = (*modeltype)(eps);
				for (int m = 0; m < AMREX_SPACEDIM; m++)
				{
					for (int n = 0; n < AMREX_SPACEDIM; n++)
						energy_box(i,j,k,0) += 0.5*sig(m,n)*eps(m,n);
				}
				//energy_box(i,j,k,0) > energy_box_old(i,j,k,0) ? energy_box(i,j,k,0) : energy_box_old(i,j,k,0);
			});
		}
	}
}

#define C_NEW(i,j,k,m) c_new(amrex::IntVect(AMREX_D_DECL(i,j,k)),m)
void
BrittleFracture::TagCellsForRefinement (int lev, amrex::TagBoxArray& tags, amrex::Real /*time*/, int /*ngrow*/)
{
	const amrex::Real* dx      = geom[lev].CellSize();
	amrex::Vector<int>  itags;
	for (amrex::MFIter mfi(*m_c[lev],true); mfi.isValid(); ++mfi)
	{
		const amrex::Box&  bx  = mfi.tilebox();
		amrex::TagBox&     tag  = tags[mfi];
		amrex::BaseFab<amrex::Real> &c_new = (*m_c[lev])[mfi];

		AMREX_D_TERM(		for (int i = bx.loVect()[0]; i<=bx.hiVect()[0]; i++),
							for (int j = bx.loVect()[1]; j<=bx.hiVect()[1]; j++),
							for (int k = bx.loVect()[2]; k<=bx.hiVect()[2]; k++)
					)
			{
				AMREX_D_TERM(	Set::Scalar gradx = (C_NEW(i+1,j,k,0) - C_NEW(i-1,j,k,0))/(2.*dx[0]);,
								Set::Scalar grady = (C_NEW(i,j+1,k,0) - C_NEW(i,j-1,k,0))/(2.*dx[1]);,
								Set::Scalar gradz = (C_NEW(i,j,k+1,0) - C_NEW(i,j,k-1,0))/(2.*dx[2]);
					)
				Set::Scalar grad = sqrt(AMREX_D_TERM(gradx*gradx, + grady*grady, + gradz*gradz));
				Set::Scalar dr = sqrt(AMREX_D_TERM(dx[0]*dx[0], + dx[1]*dx[1], + dx[2]*dx[2]));

				if(grad*dr > refinement_threshold)
					tag(amrex::IntVect(AMREX_D_DECL(i,j,k))) = amrex::TagBox::SET;
			}

	}
}

void 
BrittleFracture::Integrate(int amrlev, Set::Scalar /*time*/, int /*step*/,const amrex::MFIter &mfi, const amrex::Box &box)
{
	const amrex::Real* DX = geom[amrlev].CellSize();

	amrex::FArrayBox &c_new  = (*m_c[amrlev])[mfi];
	amrex::FArrayBox &c_old  = (*m_c_old[amrlev])[mfi];

	AMREX_D_TERM(for (int m1 = box.loVect()[0]; m1<=box.hiVect()[0]; m1++),
		     for (int m2 = box.loVect()[1]; m2<=box.hiVect()[1]; m2++),
		     for (int m3 = box.loVect()[2]; m3<=box.hiVect()[2]; m3++))
	{
		amrex::IntVect m(AMREX_D_DECL(m1,m2,m3));
		crack_err_norm += ((c_new(m,0)-c_old(m,0))*(c_new(m,0)-c_old(m,0)))*(AMREX_D_TERM(DX[0],*DX[1],*DX[2]));
		c_new_norm += c_new(m,0)*c_new(m,0)*(AMREX_D_TERM(DX[0],*DX[1],*DX[2]));
	}
}
}
