/*
 *@BEGIN LICENSE
 *
 * Libadaptive: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *@END LICENSE
 */

#ifndef _bitset_determinant_h_
#define _bitset_determinant_h_

#include <unordered_map>

#include "integrals.h"
#include "fci_vector.h"

namespace psi{ namespace forte{

/**
 * A class to store a Slater determinant using the STL bitset container.
 *
 * The determinant is represented by a pair of alpha/beta strings
 * that specify the occupation of each molecular orbital
 * (excluding frozen core and virtual orbitals).
 *
 * |Det> = |I>
 *
 * The strings are represented using one array of bits of size 2 x nmo,
 * and the following convention is used here:
 * true <-> 1
 * false <-> 0
 */
class STLBitsetDeterminant{
public:
    using bit_t = std::bitset<512>;

    //test integrals
    void test_ints(){
        outfile->Printf("\n FC energy: %1.8f", fci_ints_->frozen_core_energy());
    }

    // Class Constructor and Destructor
    /// Construct an empty determinant
    STLBitsetDeterminant();

    /// Construct the determinant from an occupation vector that
    /// specifies the alpha and beta strings.  occupation = [Ia,Ib]
    explicit STLBitsetDeterminant(int nmo);

    /// Construct the determinant from an occupation vector that
    /// specifies the alpha and beta strings.  occupation = [Ia,Ib]
    explicit STLBitsetDeterminant(const std::vector<int>& occupation);

    /// Construct the determinant from an occupation vector that
    /// specifies the alpha and beta strings.  occupation = [Ia,Ib]
    explicit STLBitsetDeterminant(const std::vector<bool>& occupation);

    /// Construct an excited determinant of a given reference
    /// Construct the determinant from two occupation vectors that
    /// specifies the alpha and beta strings.  occupation = [Ia,Ib]
    explicit STLBitsetDeterminant(const std::vector<bool>& occupation_a, const std::vector<bool>& occupation_b);

    bool operator==(const STLBitsetDeterminant& lhs) const{
        return (bits_ == lhs.bits_);
    }

    /// Get a pointer to the alpha bits
    const bit_t& bits() const {return bits_;}

    /// Return the value of an alpha bit
    bool get_alfa_bit(int n) const {return bits_[n];}
    /// Return the value of a beta bit
    bool get_beta_bit(int n) const {return bits_[n + nmo_];}

    /// Set the value of an alpha bit
    void set_alfa_bit(int n, bool value) {bits_[n] = value;}
    /// Set the value of a beta bit
    void set_beta_bit(int n, bool value) {bits_[n + nmo_] = value;}

    /// Switch the alpha and beta occupations
    void spin_flip();

    /// Return a vector of occupied alpha orbitals
    std::vector<int> get_alfa_occ();
    /// Return a vector of occupied beta orbitals
    std::vector<int> get_beta_occ();
    /// Return a vector of virtual alpha orbitals
    std::vector<int> get_alfa_vir();
    /// Return a vector of virtual beta orbitals
    std::vector<int> get_beta_vir();


    /// Return a vector of occupied alpha orbitals
    std::vector<int> get_alfa_occ() const;
    /// Return a vector of occupied beta orbitals
    std::vector<int> get_beta_occ() const;
    /// Return a vector of virtual alpha orbitals
    std::vector<int> get_alfa_vir() const;
    /// Return a vector of virtual beta orbitals
    std::vector<int> get_beta_vir() const;

    /// Set the value of an alpha bit
    double create_alfa_bit(int n);
    /// Set the value of a beta bit
    double create_beta_bit(int n);
    /// Set the value of an alpha bit
    double destroy_alfa_bit(int n);
    /// Set the value of a beta bit
    double destroy_beta_bit(int n);

    /// Print the Slater determinant
    void print() const;
    /// Save the Slater determinant as a string
    std::string str() const;

    /// Compute the energy of a Slater determinant
    double energy() const;
    double fast_energy();
    /// Compute the matrix element of the Hamiltonian between this determinant and a given one
    double slater_rules(const STLBitsetDeterminant& rhs) const;
    /// Compute the matrix element of the Hamiltonian between this determinant and a given one
    double slater_rules_single_alpha(int i, int a) const;
    /// Compute the matrix element of the Hamiltonian between this determinant and a given one
    double slater_rules_single_beta(int i, int a) const;
    /// Compute the matrix element of the S^2 operator between this determinant and a given one
    double spin2(const STLBitsetDeterminant& rhs) const;

    /// Sets the pointer to the integral object
    static void set_ints(std::shared_ptr<FCIIntegrals> ints);

public:
    // Data
    /// Number of non-frozen molecular orbitals
    int nmo_;
    /// The occupation vector (does not include the frozen orbitals)
    bit_t bits_;

    // Static data
    /// A pointer to the integral object
    static std::shared_ptr<FCIIntegrals> fci_ints_;
    /// Return the sign of a_n applied to string I
    static double SlaterSign(const bit_t& I,int n);

    struct Hash
    {
        std::size_t operator()(const psi::forte::STLBitsetDeterminant& bs) const
        {
            return std::hash<bit_t>()(bs.bits_);
        }
    };
};

typedef boost::shared_ptr<STLBitsetDeterminant> SharedSTLBitsetDeterminant;
typedef std::unordered_map<STLBitsetDeterminant,double,STLBitsetDeterminant::Hash> hash_det;

}} // End Namespaces


#endif // _bitset_determinant_h_
