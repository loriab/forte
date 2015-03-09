#include <cmath>

#include <exception>

#include <boost/numeric/odeint.hpp>

#include <libmints/molecule.h>
#include <libmints/wavefunction.h>

#include "tensorsrg.h"

using namespace std;
using namespace psi;

namespace psi{ namespace libadaptive{

TensorSRG::TensorSRG(boost::shared_ptr<Wavefunction> wfn, Options &options, ExplorerIntegrals* ints)
    : MethodBase(wfn,options,ints)
{
    startup();
}

TensorSRG::~TensorSRG()
{
    cleanup();
}

void TensorSRG::startup()
{
    outfile->Printf("\n\n      --------------------------------------");
    outfile->Printf("\n          Similarity Renormalization Group");
    outfile->Printf("\n                tensor-based code");
    outfile->Printf("\n");
    outfile->Printf("\n                Version 0.2.2");
    outfile->Printf("\n");
    outfile->Printf("\n       written by Francesco A. Evangelista");
    outfile->Printf("\n      --------------------------------------\n");
    outfile->Printf("\n      Debug level = %d",debug_);
    outfile->Printf("\n      Print level = %d\n",print_);
    outfile->Flush();

//    BlockedTensor::print_mo_spaces();

    dsrg_power_ = options_.get_double("DSRG_POWER");

    outfile->Printf("\n      Energy convergence = %e\n",options_.get_double("E_CONVERGENCE"));

    S1 = BlockedTensor::build(tensor_type_,"S1",spin_cases({"ov"}));
    S2 = BlockedTensor::build(tensor_type_,"S2",spin_cases({"oovv"}));
    DS1 = BlockedTensor::build(tensor_type_,"S1",spin_cases({"ov"}));
    DS2 = BlockedTensor::build(tensor_type_,"S2",spin_cases({"oovv"}));
    R1 = BlockedTensor::build(tensor_type_,"R1",spin_cases({"ov"}));
    R2 = BlockedTensor::build(tensor_type_,"R2",spin_cases({"oovv"}));
    Hbar1 = BlockedTensor::build(tensor_type_,"Hbar1",spin_cases({"ii"}));
    Hbar2 = BlockedTensor::build(tensor_type_,"Hbar2",spin_cases({"iiii"}));
    eta1 = BlockedTensor::build(tensor_type_,"eta1",spin_cases({"ii"}));
    eta2 = BlockedTensor::build(tensor_type_,"eta2",spin_cases({"iiii"}));
    g1 = BlockedTensor::build(tensor_type_,"g1",spin_cases({"ii"}));
    g2 = BlockedTensor::build(tensor_type_,"g2",spin_cases({"iiii"}));
    O1 = BlockedTensor::build(tensor_type_,"O1",spin_cases({"ii"}));
    O2 = BlockedTensor::build(tensor_type_,"O2",spin_cases({"iiii"}));
    C1 = BlockedTensor::build(tensor_type_,"C1",spin_cases({"ii"}));
    C2 = BlockedTensor::build(tensor_type_,"C2",spin_cases({"iiii"}));
    D1 = BlockedTensor::build(tensor_type_,"D1",spin_cases({"ov"}));
    D2 = BlockedTensor::build(tensor_type_,"D2",spin_cases({"oovv"}));
    RInvD1 = BlockedTensor::build(tensor_type_,"InvRD1",spin_cases({"ov"}));
    RInvD2 = BlockedTensor::build(tensor_type_,"InvRD2",spin_cases({"oovv"}));
    I_ioiv = BlockedTensor::build(tensor_type_,"I_ioiv",spin_cases({"ioiv"}));

    size_t ncmo_ = ints_->ncmo();
    std::vector<double> Fa(ncmo_);
    std::vector<double> Fb(ncmo_);

    F.iterate([&](const std::vector<size_t>& i,const std::vector<SpinType>& spin,double& value){
        if (spin[0] == AlphaSpin and (i[0] == i[1])) Fa[i[0]] = value;
        if (spin[0] ==  BetaSpin and (i[0] == i[1])) Fb[i[0]] = value;
    });

    double srg_s = options_.get_double("DSRG_S");

    D1.iterate([&](const std::vector<size_t>& i,const std::vector<SpinType>& spin,double& value){
        if (spin[0]  == AlphaSpin){
            value = Fa[i[0]] - Fa[i[1]];
        }else if (spin[0]  == BetaSpin){
            value = Fb[i[0]] - Fb[i[1]];
        }
    });

    D2.iterate([&](const std::vector<size_t>& i,const std::vector<SpinType>& spin,double& value){
        if ((spin[0] == AlphaSpin) and (spin[1] == AlphaSpin)){
            value = Fa[i[0]] + Fa[i[1]] - Fa[i[2]] - Fa[i[3]];
        }else if ((spin[0] == AlphaSpin) and (spin[1] == BetaSpin) ){
            value = Fa[i[0]] + Fb[i[1]] - Fa[i[2]] - Fb[i[3]];
        }else if ((spin[0] == BetaSpin)  and (spin[1] == BetaSpin) ){
            value = Fb[i[0]] + Fb[i[1]] - Fb[i[2]] - Fb[i[3]];
        }
    });

    RInvD1.iterate([&](const std::vector<size_t>& i,const std::vector<SpinType>& spin,double& value){
        if (spin[0]  == AlphaSpin){
            value = one_minus_exp_div_x(srg_s,Fa[i[0]] - Fa[i[1]],dsrg_power_);
        }else if (spin[0]  == BetaSpin){
            value = one_minus_exp_div_x(srg_s,Fb[i[0]] - Fb[i[1]],dsrg_power_);
        }
    });

    RInvD2.iterate([&](const std::vector<size_t>& i,const std::vector<SpinType>& spin,double& value){
        if ((spin[0] == AlphaSpin) and (spin[1] == AlphaSpin)){
            value = one_minus_exp_div_x(srg_s,Fa[i[0]] + Fa[i[1]] - Fa[i[2]] - Fa[i[3]],dsrg_power_);
        }else if ((spin[0] == AlphaSpin) and (spin[1] == BetaSpin) ){
            value = one_minus_exp_div_x(srg_s,Fa[i[0]] + Fb[i[1]] - Fa[i[2]] - Fb[i[3]],dsrg_power_);
        }else if ((spin[0] == BetaSpin)  and (spin[1] == BetaSpin) ){
            value = one_minus_exp_div_x(srg_s,Fb[i[0]] + Fb[i[1]] - Fb[i[2]] - Fb[i[3]],dsrg_power_);
        }
    });
}

void TensorSRG::cleanup()
{
    print_timings();
}

double TensorSRG::compute_mp2_guess()
{
    S2["ijab"] = V["ijab"] * InvD2["ijab"];
    S2["iJaB"] = V["iJaB"] * InvD2["iJaB"];
    S2["IJAB"] = V["IJAB"] * InvD2["IJAB"];

    double Eaa = 0.25 * S2["ijab"] * V["ijab"];
    double Eab = S2["iJaB"] * V["iJaB"];
    double Ebb = 0.25 * S2["IJAB"] * V["IJAB"];

    double mp2_correlation_energy = Eaa + Eab + Ebb;
    double ref_energy = reference_energy();
    outfile->Printf("\n\n    SCF energy                            = %20.15f",ref_energy);
    outfile->Printf("\n    SRG-PT2 correlation energy            = %20.15f",mp2_correlation_energy);
    outfile->Printf("\n  * SRG-PT2 total energy                  = %20.15f\n",ref_energy + mp2_correlation_energy);

//    outfile->Printf("\n\n    SCF energy                            = %20.15f",E0_);
//    outfile->Printf("\n\n    SCF energy                            = %20.15f",E0_);
//    outfile->Printf("\n    MP2 correlation energy                = %20.15f",mp2_correlation_energy);
//    outfile->Printf("\n  * MP2 total energy                      = %20.15f\n",E0_ + mp2_correlation_energy);
    return E0_ + mp2_correlation_energy;
}

double TensorSRG::compute_mp2_guess_driven_srg()
{
    double srg_s = options_.get_double("DSRG_S");

    S2.zero();
    S2["ijab"] = V["ijab"] * RInvD2["ijab"];
    S2["iJaB"] = V["iJaB"] * RInvD2["iJaB"];
    S2["IJAB"] = V["IJAB"] * RInvD2["IJAB"];

    outfile->Printf("\n\n ||V||      = %20.15f",V.norm());
    outfile->Printf("\n\n ||RInvD2|| = %20.15f",RInvD2.norm());

    double Eaa = 0.25 * S2["ijab"] * V["ijab"];
    double Eab = S2["iJaB"] * V["iJaB"];
    double Ebb = 0.25 * S2["IJAB"] * V["IJAB"];

    double mp2_correlation_energy = Eaa + Eab + Ebb;
    double ref_energy = reference_energy();
    outfile->Printf("\n\n    SCF energy                            = %20.15f",ref_energy);
    outfile->Printf("\n    SRG-PT2 correlation energy            = %20.15f",mp2_correlation_energy);
    outfile->Printf("\n  * SRG-PT2 total energy                  = %20.15f\n",ref_energy + mp2_correlation_energy);
    return ref_energy + mp2_correlation_energy;
}

double one_minus_exp_div_x(double s,double x,double power)
{
    return ((1.0 - std::exp(-s * std::pow(x,power))) / x);
}

double TensorSRG::compute_energy()
{
    if(options_.get_str("SRG_MODE") == "SRG"){
        compute_srg_energy();
    }else if(options_.get_str("SRG_MODE") == "CT"){
        return compute_ct_energy();
    }else if(options_.get_str("SRG_MODE") == "DSRG"){
//        compute_driven_srg_energy();
    }
    return 0.0;
}

void TensorSRG::transfer_integrals()
{
    throw std::runtime_error("TensorSRG::transfer_integrals() is not implemented yet!");
    /*
    // Scalar term
    double scalar0 = E0_ + Hbar0;
    double scalar1 = 0.0;
    double scalar2 = 0.0;
    {
        Tensor& Hbar_oo = *Hbar1.block("oo");
        Tensor::iterator it = Hbar_oo.begin();
        Tensor::iterator endit = Hbar_oo.end();
        for (; it != endit; ++it){
            std::vector<size_t>& i = it.address();
            if (i[0] == i[1]){
                scalar1 -= *it;
            }
        }
    }
    {
        Tensor& Hbar_OO = *Hbar1.block("OO");
        Tensor::iterator it = Hbar_OO.begin();
        Tensor::iterator endit = Hbar_OO.end();
        for (; it != endit; ++it){
            std::vector<size_t>& i = it.address();
            if (i[0] == i[1]){
                scalar1 -= *it;
            }
        }
    }
    {
        Tensor& Hbar_oooo = *Hbar2.block("oooo");
        Tensor::iterator it = Hbar_oooo.begin();
        Tensor::iterator endit = Hbar_oooo.end();
        for (; it != endit; ++it){
            std::vector<size_t>& i = it.address();
            if ((i[0] == i[2]) and (i[1] == i[3])){
                scalar2 += 0.5 * (*it);
            }
        }
    }
    {
        Tensor& Hbar_oOoO = *Hbar2.block("oOoO");
        Tensor::iterator it = Hbar_oOoO.begin();
        Tensor::iterator endit = Hbar_oOoO.end();
        for (; it != endit; ++it){
            std::vector<size_t>& i = it.address();
            if ((i[0] == i[2]) and (i[1] == i[3])){
                scalar2 += (*it);
            }
        }
    }
    {
        Tensor& Hbar_OOOO = *Hbar2.block("OOOO");
        Tensor::iterator it = Hbar_OOOO.begin();
        Tensor::iterator endit = Hbar_OOOO.end();
        for (; it != endit; ++it){
            std::vector<size_t>& i = it.address();
            if ((i[0] == i[2]) and (i[1] == i[3])){
                scalar2 += 0.5 * (*it);
            }
        }
    }
    double scalar = scalar0 + scalar1 + scalar2 - molecule()->nuclear_repulsion_energy();
    outfile->Printf("\n  The Hamiltonian electronic scalar term (normal ordered wrt the true vacuum");
    outfile->Printf("\n  E0 = %20.12f",scalar);

    O1["pq"] = Hbar1["pq"];
    O1["PQ"] = Hbar1["PQ"];
    O1["pq"] -= Hbar2["prqs"] * G1["rs"];
    O1["pq"] -= Hbar2["pRqS"] * G1["RS"];
    O1["PQ"] -= Hbar2["rPsQ"] * G1["rs"];
    O1["PQ"] -= Hbar2["PRQS"] * G1["RS"];

    outfile->Printf("\n  Updating all the integrals");
    ints_->set_scalar(scalar);
    O1.iterate_over_elements([&](std::vector<size_t>& m,std::vector<MOSetSpinType>& spin,double& value)
    {
        if ((spin[0] == Alpha) and (spin[1] == Alpha)){
            ints_->set_oei(m[0],m[1],value,true);
        }
        if ((spin[0] == Beta) and (spin[1] == Beta)){
            ints_->set_oei(m[0],m[1],value,false);
        }
    });
    Hbar2.iterate_over_elements([&](std::vector<size_t>& m,std::vector<MOSetSpinType>& spin,double& value){
        if ((spin[0] == Alpha) and (spin[1] == Alpha) and (spin[2] == Alpha) and (spin[3] == Alpha)){
            ints_->set_tei(m[0],m[1],m[2],m[3],value,true,true);
        }
        if ((spin[0] == Alpha) and (spin[1] == Beta) and (spin[2] == Alpha) and (spin[3] == Beta)){
            ints_->set_tei(m[0],m[1],m[2],m[3],value,true,false);
        }
        if ((spin[0] == Beta) and (spin[1] == Beta) and (spin[2] == Beta) and (spin[3] == Beta)){
            ints_->set_tei(m[0],m[1],m[2],m[3],value,false,false);
        }
    });

    // As a test compute the current CT energy
    double Esth = scalar;
    {
        Tensor& Hbar_oo = *O1.block("oo");
        Tensor::iterator it = Hbar_oo.begin();
        Tensor::iterator endit = Hbar_oo.end();
        for (; it != endit; ++it){
            std::vector<size_t>& i = it.address();
            if (i[0] == i[1]){
                Esth += *it;
            }
        }
    }
    {
        Tensor& Hbar_OO = *O1.block("OO");
        Tensor::iterator it = Hbar_OO.begin();
        Tensor::iterator endit = Hbar_OO.end();
        for (; it != endit; ++it){
            std::vector<size_t>& i = it.address();
            if (i[0] == i[1]){
                Esth += *it;
            }
        }
    }
    {
        Tensor& Hbar_oooo = *Hbar2.block("oooo");
        Tensor::iterator it = Hbar_oooo.begin();
        Tensor::iterator endit = Hbar_oooo.end();
        for (; it != endit; ++it){
            std::vector<size_t>& i = it.address();
            if ((i[0] == i[2]) and (i[1] == i[3])){
                Esth += 0.5 * (*it);
            }
        }
    }
    {
        Tensor& Hbar_oOoO = *Hbar2.block("oOoO");
        Tensor::iterator it = Hbar_oOoO.begin();
        Tensor::iterator endit = Hbar_oOoO.end();
        for (; it != endit; ++it){
            std::vector<size_t>& i = it.address();
            if ((i[0] == i[2]) and (i[1] == i[3])){
                Esth += (*it);
            }
        }
    }
    {
        Tensor& Hbar_OOOO = *Hbar2.block("OOOO");
        Tensor::iterator it = Hbar_OOOO.begin();
        Tensor::iterator endit = Hbar_OOOO.end();
        for (; it != endit; ++it){
            std::vector<size_t>& i = it.address();
            if ((i[0] == i[2]) and (i[1] == i[3])){
                Esth += 0.5 * (*it);
            }
        }
    }

    outfile->Printf("\n  <H> = %24.12f",Esth + molecule()->nuclear_repulsion_energy());

    ints_->update_integrals();
    outfile->Flush();
    */
}

}} // EndNamespaces
