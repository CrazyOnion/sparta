/* ----------------------------------------------------------------------
   SPARTA - Stochastic PArallel Rarefied-gas Time-accurate Analyzer
   http://sparta.sandia.gov
   Steve Plimpton, sjplimp@sandia.gov, Michael Gallis, magalli@sandia.gov
   Sandia National Laboratories

   Copyright (2014) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level SPARTA directory.
------------------------------------------------------------------------- */

#ifdef SURF_COLLIDE_CLASS

SurfCollideStyle(diffuse/kk,SurfCollideDiffuseKokkos)

#else

#ifndef SPARTA_SURF_COLLIDE_DIFFUSE_KOKKOS_H
#define SPARTA_SURF_COLLIDE_DIFFUSE_KOKKOS_H

#include "surf_collide_diffuse.h"
#include "kokkos_type.h"
#include "kokkos_type.h"
#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "surf_collide_diffuse_kokkos.h"
#include "surf.h"
#include "surf_react.h"
#include "input.h"
#include "variable.h"
#include "particle.h"
#include "domain.h"
#include "update.h"
#include "modify.h"
#include "comm.h"
#include "random_mars.h"
#include "random_knuth.h"
#include "math_const.h"
#include "math_extra_kokkos.h"
#include "error.h"
#include "Kokkos_Random.hpp"
#include "rand_pool_wrap.h"

namespace SPARTA_NS {

enum{NONE,DISCRETE,SMOOTH};            // several files

class SurfCollideDiffuseKokkos : public SurfCollideDiffuse {
 public:

  SurfCollideDiffuseKokkos(class SPARTA *, int, char **);
  SurfCollideDiffuseKokkos(class SPARTA *);
  ~SurfCollideDiffuseKokkos();

  void pre_collide();

#ifndef SPARTA_KOKKOS_EXACT
  Kokkos::Random_XorShift64_Pool<DeviceType> rand_pool;
  typedef typename Kokkos::Random_XorShift64_Pool<DeviceType>::generator_type rand_type;
#else
  RandPoolWrap rand_pool;
  typedef RandWrap rand_type;
#endif
//Kokkos::Random_XorShift1024_Pool<DeviceType> rand_pool;
//typedef typename Kokkos::Random_XorShift1024_Pool<DeviceType>::generator_type rand_type;

  /* ----------------------------------------------------------------------
     particle collision with surface with optional chemistry
     ip = particle with current x = collision pt, current v = incident v
     norm = surface normal unit vector
     isr = index of reaction model if >= 0, -1 for no chemistry
     ip = set to NULL if destroyed by chemsitry
     return jp = new particle if created by chemistry
     return reaction = index of reaction (1 to N) that took place, 0 = no reaction
     resets particle(s) to post-collision outward velocity
  ------------------------------------------------------------------------- */

  KOKKOS_INLINE_FUNCTION
  Particle::OnePart* collide_kokkos(Particle::OnePart *&ip, const double *norm, double &, int, int &) const
  {
    Kokkos::atomic_fetch_add(&d_nsingle(),1);

    // if surface chemistry defined, attempt reaction
    // reaction > 0 if reaction took place

    //Particle::OnePart iorig;
    Particle::OnePart *jp = NULL;
    //reaction = 0;

    //if (isr >= 0) {
    //  if (modify->n_surf_react) memcpy(&iorig,ip,sizeof(Particle::OnePart));
    //  reaction = surf->sr[isr]->react(ip,norm,jp);
    //  if (reaction) surf->nreact_one++;
    //}

    // diffuse reflection for each particle
    // resets v, roteng, vibeng
    // particle I needs to trigger any fixes to update per-particle
    //  properties which depend on the temperature of the particle
    //  (e.g. fix vibmode and fix ambipolar)
    // if new particle J created, also need to trigger any fixes

    if (ip) {
      diffuse(ip,norm);
      //if (modify->n_update_custom) {
      //  int i = ip - particle->particles;
      //  modify->update_custom(i,twall,twall,twall,vstream);
      //}
    }
    //if (jp) {
    //  diffuse(jp,norm);
    //  if (modify->n_update_custom) {
    //    int j = jp - particle->particles;
    //    modify->update_custom(j,twall,twall,twall,vstream);
    //  }
    //}

    // call any fixes with a surf_react() method
    // they may reset j to -1, e.g. fix ambipolar
    //   in which case newly created j is deleted

    //if (reaction && modify->n_surf_react) {
    //  int i = -1;
    //  if (ip) i = ip - particle->particles;
    //  int j = -1;
    //  if (jp) j = jp - particle->particles;
    //  modify->surf_react(&iorig,i,j);
    //  if (jp && j < 0) {
    //    jp = NULL;
    //    particle->nlocal--;
    //  }
    //}

    return jp;
  };

  DAT::tdual_int_scalar k_nsingle;
  DAT::t_int_scalar d_nsingle;
  HAT::t_int_scalar h_nsingle;

 private:
  double boltz;
  t_species_1d d_species;
  int rotstyle, vibstyle;

  KOKKOS_INLINE_FUNCTION
  void diffuse(Particle::OnePart *p, const double *norm) const
  {
    // specular reflection
    // reflect incident v around norm

    rand_type rand_gen = rand_pool.get_state();

    if (rand_gen.drand() > acc) {
      MathExtraKokkos::reflect3(p->v,norm);

    // diffuse reflection
    // vrm = most probable speed of species, eqns (4.1) and (4.7)
    // vperp = velocity component perpendicular to surface along norm, eqn (12.3)
    // vtan12 = 2 velocity components tangential to surface
    // tangent1 = component of particle v tangential to surface,
    //   check if tangent1 = 0 (normal collision), set randomly
    // tangent2 = norm x tangent1 = orthogonal tangential direction
    // tangent12 are both unit vectors

    } else {
      double tangent1[3],tangent2[3];
      int ispecies = p->ispecies;

      double vrm = sqrt(2.0*boltz * twall / d_species[ispecies].mass);
      double vperp = vrm * sqrt(-log(rand_gen.drand()));

      double theta = MathConst::MY_2PI * rand_gen.drand();
      double vtangent = vrm * sqrt(-log(rand_gen.drand()));
      double vtan1 = vtangent * sin(theta);
      double vtan2 = vtangent * cos(theta);

      double *v = p->v;
      double dot = MathExtraKokkos::dot3(v,norm);

      double beta_un,normalized_distbn_fn;

      tangent1[0] = v[0] - dot*norm[0];
      tangent1[1] = v[1] - dot*norm[1];
      tangent1[2] = v[2] - dot*norm[2];

      if (MathExtraKokkos::lensq3(tangent1) == 0.0) {
        tangent2[0] = rand_gen.drand();
        tangent2[1] = rand_gen.drand();
        tangent2[2] = rand_gen.drand();
        MathExtraKokkos::cross3(norm,tangent2,tangent1);
      }

      MathExtraKokkos::norm3(tangent1);
      MathExtraKokkos::cross3(norm,tangent1,tangent2);

      // add in translation or rotation vector if specified
      // only keep portion of vector tangential to surface element

      if (trflag) {
        double vxdelta,vydelta,vzdelta;
        if (tflag) {
          vxdelta = vx; vydelta = vy; vzdelta = vz;
          double dot = vxdelta*norm[0] + vydelta*norm[1] + vzdelta*norm[2];

          if (fabs(dot) > 0.001) {
            dot /= vrm;
            do {
              do {
                beta_un = (6.0*rand_gen.drand() - 3.0);
              } while (beta_un + dot < 0.0);
              normalized_distbn_fn = 2.0 * (beta_un + dot) /
                (dot + sqrt(dot*dot + 2.0)) *
                exp(0.5 + (0.5*dot)*(dot-sqrt(dot*dot + 2.0)) -
                    beta_un*beta_un);
            } while (normalized_distbn_fn < rand_gen.drand());
            vperp = beta_un*vrm;
          }

        } else {
          double *x = p->x;
          vxdelta = wy*(x[2]-pz) - wz*(x[1]-py);
          vydelta = wz*(x[0]-px) - wx*(x[2]-pz);
          vzdelta = wx*(x[1]-py) - wy*(x[0]-px);
          double dot = vxdelta*norm[0] + vydelta*norm[1] + vzdelta*norm[2];
          vxdelta -= dot*norm[0];
          vydelta -= dot*norm[1];
          vzdelta -= dot*norm[2];
        }

        v[0] = vperp*norm[0] + vtan1*tangent1[0] + vtan2*tangent2[0] + vxdelta;
        v[1] = vperp*norm[1] + vtan1*tangent1[1] + vtan2*tangent2[1] + vydelta;
        v[2] = vperp*norm[2] + vtan1*tangent1[2] + vtan2*tangent2[2] + vzdelta;

      // no translation or rotation

      } else {
        v[0] = vperp*norm[0] + vtan1*tangent1[0] + vtan2*tangent2[0];
        v[1] = vperp*norm[1] + vtan1*tangent1[1] + vtan2*tangent2[1];
        v[2] = vperp*norm[2] + vtan1*tangent1[2] + vtan2*tangent2[2];
      }

      // initialize rot/vib energy

      p->erot = erot(ispecies,twall,rand_gen,boltz);
      p->evib = evib(ispecies,twall,rand_gen,boltz);
    }
    rand_pool.free_state(rand_gen);
  }

  /* ----------------------------------------------------------------------
     generate random rotational energy for a particle
     only a function of species index and species properties
  ------------------------------------------------------------------------- */

  KOKKOS_INLINE_FUNCTION
  double erot(int isp, double temp_thermal, rand_type &rand_gen, double boltz) const
  {
    double eng,a,erm,b;

    if (rotstyle == NONE) return 0.0;
    if (d_species[isp].rotdof < 2) return 0.0;

    if (rotstyle == DISCRETE && d_species[isp].rotdof == 2) {
      int irot = -log(rand_gen.drand()) * temp_thermal /
        d_species[isp].rottemp[0];
      eng = irot * boltz * d_species[isp].rottemp[0];
    } else if (rotstyle == SMOOTH && d_species[isp].rotdof == 2) {
      eng = -log(rand_gen.drand()) * boltz * temp_thermal;
    } else {
      a = 0.5*d_species[isp].rotdof-1.0;
      while (1) {
        // energy cut-off at 10 kT
        erm = 10.0*rand_gen.drand();
        b = pow(erm/a,a) * exp(a-erm);
        if (b > rand_gen.drand()) break;
      }
      eng = erm * boltz * temp_thermal;
    }

   return eng;
  }

  /* ----------------------------------------------------------------------
     generate random vibrational energy for a particle
     only a function of species index and species properties
     index_vibmode = index of extra per-particle vibrational mode storage
       -1 if not defined for this model
  ------------------------------------------------------------------------- */

  KOKKOS_INLINE_FUNCTION
  double evib(int isp, double temp_thermal, rand_type &rand_gen, double boltz) const
  {
    double eng,a,erm,b;

    if (vibstyle == NONE || d_species[isp].vibdof < 2) return 0.0;

    // for DISCRETE, only need set evib for vibdof = 2
    // mode levels and evib will be set by FixVibmode::update_custom()

    eng = 0.0;

    if (vibstyle == DISCRETE && d_species[isp].vibdof == 2) {
      int ivib = -log(rand_gen.drand()) * temp_thermal /
        d_species[isp].vibtemp[0];
      eng = ivib * boltz * d_species[isp].vibtemp[0];
    } else if (vibstyle == SMOOTH || d_species[isp].vibdof >= 2) {
      if (d_species[isp].vibdof == 2)
        eng = -log(rand_gen.drand()) * boltz * temp_thermal;
      else if (d_species[isp].vibdof > 2) {
        a = 0.5*d_species[isp].vibdof-1.;
        while (1) {
          // energy cut-off at 10 kT
          erm = 10.0*rand_gen.drand();
          b = pow(erm/a,a) * exp(a-erm);
          if (b > rand_gen.drand()) break;
        }
        eng = erm * boltz * temp_thermal;
      }
    }

    return eng;
  }
};

}

#endif
#endif

/* ERROR/WARNING messages:

E: Illegal ... command

Self-explanatory.  Check the input script syntax and compare to the
documentation for the command.  You can use -echo screen as a
command-line option when running SPARTA to see the offending line.

E: Surf_collide diffuse rotation invalid for 2d

Specified rotation vector must be in z-direction.

E: Surf_collide diffuse variable name does not exist

Self-explanatory.

E: Surf_collide diffuse variable is invalid style

It must be an equal-style variable.

*/
