
#include <algorithm>
#include <cfloat>
#include <iostream>
#include <iomanip>
#include <map>
#include <limits>

#include <AMReX_BLassert.H>
#include <AMReX_MultiFab.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_BLProfiler.H>
#include <AMReX_iMultiFab.H>
#include <AMReX_BaseFab_f.H>

#ifdef BL_MEM_PROFILING
#include <AMReX_MemProfiler.H>
#endif

#ifdef AMREX_USE_CUDA
#include <AMReX_Managed.H>
#include <AMReX_CUDA_Utility.H>
#endif

namespace amrex {

namespace
{
    bool initialized = false;
#ifdef BL_MEM_PROFILING
    int num_multifabs     = 0;
    int num_multifabs_hwm = 0;
#endif
}

#if !defined(BL_NO_FORT)

Real
MultiFab::Dot (const MultiFab& x, int xcomp,
	       const MultiFab& y, int ycomp,
	       int numcomp, int nghost, bool local)
{
    BL_ASSERT(x.boxArray() == y.boxArray());
    BL_ASSERT(x.DistributionMap() == y.DistributionMap());
    BL_ASSERT(x.nGrow() >= nghost && y.nGrow() >= nghost);

    Real sm = 0.0;

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel if (!system::regtest_reduction) reduction(+:sm)
#endif
    for (MFIter mfi(x,true); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);
        sm += x[mfi].dot(bx,xcomp,y[mfi],bx,ycomp,numcomp);
    }

    if (!local)
        ParallelAllReduce::Sum(sm, ParallelContext::CommunicatorSub());

    return sm;
}

Real
MultiFab::Dot (const iMultiFab& mask,
               const MultiFab& x, int xcomp,
	       const MultiFab& y, int ycomp,
	       int numcomp, int nghost, bool local)
{
    BL_ASSERT(x.boxArray() == y.boxArray());
    BL_ASSERT(x.boxArray() == mask.boxArray());
    BL_ASSERT(x.DistributionMap() == y.DistributionMap());
    BL_ASSERT(x.DistributionMap() == mask.DistributionMap());
    BL_ASSERT(x.nGrow() >= nghost && y.nGrow() >= nghost);
    BL_ASSERT(mask.nGrow() >= nghost);

    Real sm = 0.0;

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel if (!system::regtest_reduction) reduction(+:sm)
#endif
    for (MFIter mfi(x,true); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);
        sm += x[mfi].dotmask(mask[mfi],bx,xcomp,y[mfi],bx,ycomp,numcomp);
    }

    if (!local)
        ParallelAllReduce::Sum(sm, ParallelContext::CommunicatorSub());

    return sm;
}
#endif

void
MultiFab::Add (MultiFab&       dst,
	       const MultiFab& src,
	       int             srccomp,
	       int             dstcomp,
	       int             numcomp,
	       int             nghost)
{
    Add(dst,src,srccomp,dstcomp,numcomp,IntVect(nghost));
}

void
MultiFab::Add (MultiFab&       dst,
	       const MultiFab& src,
	       int             srccomp,
	       int             dstcomp,
	       int             numcomp,
	       const IntVect&  nghost)
{
    BL_ASSERT(dst.boxArray() == src.boxArray());
    BL_ASSERT(dst.distributionMap == src.distributionMap);
    BL_ASSERT(dst.nGrowVect().allGE(nghost) && src.nGrowVect().allGE(nghost));

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(dst,TrueUnlessGPU); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);
        const FArrayBox* srcFab = &(src[mfi]);
              FArrayBox* dstFab = &(dst[mfi]);

        if (bx.ok())
        {
            AMREX_BOX_L_LAUNCH(bx,
            [=] AMREX_CUDA_DEVICE ()
            {
               dstFab->plus(*srcFab, bx, bx, srccomp, dstcomp, numcomp);
            });
        }
    }
}

void
MultiFab::Copy (MultiFab&       dst,
                const MultiFab& src,
                int             srccomp,
                int             dstcomp,
                int             numcomp,
                int             nghost)
{
    Copy(dst,src,srccomp,dstcomp,numcomp,IntVect(nghost));
}

void
MultiFab::Copy (MultiFab&       dst,
                const MultiFab& src,
                int             srccomp,
                int             dstcomp,
                int             numcomp,
                const IntVect&  nghost)
{
// don't have to BL_ASSERT(dst.boxArray() == src.boxArray());
    BL_ASSERT(dst.distributionMap == src.distributionMap);
    BL_ASSERT(dst.nGrowVect().allGE(nghost));

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(dst,TrueUnlessGPU); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);
        const FArrayBox* srcFab = &(src[mfi]);
              FArrayBox* dstFab = &(dst[mfi]);

       if (bx.ok())
       {
          AMREX_BOX_L_LAUNCH(bx,
          [=] AMREX_CUDA_DEVICE ()
          {
             dstFab->copy(*srcFab, bx, srccomp, bx, dstcomp, numcomp);
          });
       }
    }
}


#ifdef USE_PERILLA
void
MultiFab::Copy (MultiFab&       dst,
                const MultiFab& src,
                int             f,
                int             srccomp,
                int             dstcomp,
                int             numcomp,
                const Box&      bx)
{
// don't have to    BL_ASSERT(dst.boxArray() == src.boxArray());
    BL_ASSERT(dst.distributionMap == src.distributionMap);
    //BL_ASSERT(dst.nGrow() >= nghost); // && src.nGrow() >= nghost);

    int fis = src.IndexArray()[f];
    int fid = dst.IndexArray()[f];
    //const Box& bx = BoxLib::grow(dst[f].box(),nghost);
    //const Box& bx = dst[fid].box();

    if (bx.ok())
      dst[fid].copy(src[fid], bx, srccomp, bx, dstcomp, numcomp);

}
#endif


void
MultiFab::Subtract (MultiFab&       dst,
		    const MultiFab& src,
		    int             srccomp,
		    int             dstcomp,
		    int             numcomp,
		    int             nghost)
{
    Subtract(dst,src,srccomp,dstcomp,numcomp,IntVect(nghost));
}

void
MultiFab::Subtract (MultiFab&       dst,
		    const MultiFab& src,
		    int             srccomp,
		    int             dstcomp,
		    int             numcomp,
		    const IntVect&  nghost)
{
    BL_ASSERT(dst.boxArray() == src.boxArray());
    BL_ASSERT(dst.distributionMap == src.distributionMap);
    BL_ASSERT(dst.nGrowVect().allGE(nghost) && src.nGrowVect().allGE(nghost));

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(dst,TrueUnlessGPU); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);
        const FArrayBox* srcFab = &(src[mfi]);
              FArrayBox* dstFab = &(dst[mfi]);

        if (bx.ok())
        {
            AMREX_BOX_L_LAUNCH(bx,
            [=] AMREX_CUDA_DEVICE ()
            {
                dstFab->minus(*srcFab, bx, bx, srccomp, dstcomp, numcomp);
            });
        }
    }
}

void
MultiFab::Multiply (MultiFab&       dst,
		    const MultiFab& src,
		    int             srccomp,
		    int             dstcomp,
		    int             numcomp,
		    int             nghost)
{
    Multiply(dst,src,srccomp,dstcomp,numcomp,IntVect(nghost));
}

void
MultiFab::Multiply (MultiFab&       dst,
		    const MultiFab& src,
		    int             srccomp,
		    int             dstcomp,
		    int             numcomp,
		    const IntVect&  nghost)
{
    BL_ASSERT(dst.boxArray() == src.boxArray());
    BL_ASSERT(dst.distributionMap == src.distributionMap);
    BL_ASSERT(dst.nGrowVect().allGE(nghost) && src.nGrowVect().allGE(nghost));

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(dst,TrueUnlessGPU); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);
        const FArrayBox* srcFab = &(src[mfi]);
              FArrayBox* dstFab = &(dst[mfi]);

        if (bx.ok())
        {
            AMREX_BOX_L_LAUNCH(bx,
            [=] AMREX_CUDA_DEVICE ()
            {
                dstFab->mult(*srcFab, bx, bx, srccomp, dstcomp, numcomp);
            });
        }
    }
}

void
MultiFab::Divide (MultiFab&       dst,
		  const MultiFab& src,
		  int             srccomp,
		  int             dstcomp,
		  int             numcomp,
		  int             nghost)
{
    Divide(dst,src,srccomp,dstcomp,numcomp,IntVect(nghost));
}

void
MultiFab::Divide (MultiFab&       dst,
		  const MultiFab& src,
		  int             srccomp,
		  int             dstcomp,
		  int             numcomp,
		  const IntVect&  nghost)
{
    BL_ASSERT(dst.boxArray() == src.boxArray());
    BL_ASSERT(dst.distributionMap == src.distributionMap);
    BL_ASSERT(dst.nGrowVect().allGE(nghost) && src.nGrowVect().allGE(nghost));

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(dst,TrueUnlessGPU); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);
        const FArrayBox* srcFab = &(src[mfi]);
              FArrayBox* dstFab = &(dst[mfi]);

        if (bx.ok())
        {
            AMREX_BOX_L_LAUNCH(bx,
            [=] AMREX_CUDA_DEVICE ()
            {
                dstFab->divide(*srcFab, bx, bx, srccomp, dstcomp, numcomp);
            });
        }
    }
}

#if !defined(BL_NO_FORT)

void
MultiFab::Saxpy (MultiFab&       dst,
		 Real            a, 
		 const MultiFab& src,
		 int             srccomp,
		 int             dstcomp,
		 int             numcomp,
		 int             nghost)
{
    BL_ASSERT(dst.boxArray() == src.boxArray());
    BL_ASSERT(dst.distributionMap == src.distributionMap);
    BL_ASSERT(dst.nGrow() >= nghost && src.nGrow() >= nghost);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(dst,true); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);

        if (bx.ok())
            dst[mfi].saxpy(a, src[mfi], bx, bx, srccomp, dstcomp, numcomp);
    }
}

void
MultiFab::Xpay (MultiFab&       dst,
		Real            a, 
		const MultiFab& src,
		int             srccomp,
		int             dstcomp,
		int             numcomp,
		int             nghost)
{
    BL_ASSERT(dst.boxArray() == src.boxArray());
    BL_ASSERT(dst.distributionMap == src.distributionMap);
    BL_ASSERT(dst.nGrow() >= nghost && src.nGrow() >= nghost);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(dst,true); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);

        if (bx.ok())
            dst[mfi].xpay(a, src[mfi], bx, bx, srccomp, dstcomp, numcomp);
    }
}

void
MultiFab::LinComb (MultiFab&       dst,
		   Real            a,
		   const MultiFab& x,
		   int             xcomp,
		   Real            b,
		   const MultiFab& y,
		   int             ycomp,
		   int             dstcomp,
		   int             numcomp,
		   int             nghost)
{
    BL_ASSERT(dst.boxArray() == x.boxArray());
    BL_ASSERT(dst.distributionMap == x.distributionMap);
    BL_ASSERT(dst.boxArray() == y.boxArray());
    BL_ASSERT(dst.distributionMap == y.distributionMap);
    BL_ASSERT(dst.nGrow() >= nghost && x.nGrow() >= nghost && y.nGrow() >= nghost);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(dst,true); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);
	
        if (bx.ok())
            dst[mfi].linComb(x[mfi],bx,xcomp,y[mfi],bx,ycomp,a,b,bx,dstcomp,numcomp);
    }
}

void
MultiFab::AddProduct (MultiFab&       dst,
		      const MultiFab& src1,
		      int             comp1,
		      const MultiFab& src2,
		      int             comp2,
		      int             dstcomp,
		      int             numcomp,
		      int             nghost)
{
    BL_ASSERT(dst.boxArray() == src1.boxArray());
    BL_ASSERT(dst.distributionMap == src1.distributionMap);
    BL_ASSERT(dst.boxArray() == src2.boxArray());
    BL_ASSERT(dst.distributionMap == src2.distributionMap);
    BL_ASSERT(dst.nGrow() >= nghost && src1.nGrow() >= nghost && src2.nGrow() >= nghost);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(dst,true); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);
	
        if (bx.ok())
            dst[mfi].addproduct(bx, dstcomp, numcomp, src1[mfi], comp1, src2[mfi], comp2);
    }
}

#endif

void
MultiFab::plus (Real val,
                int  nghost)
{
    plus(val,0,n_comp,nghost);
}

void
MultiFab::plus (Real       val,
                const Box& region,
                int        nghost)
{
    plus(val,region,0,n_comp,nghost);
}

void
MultiFab::mult (Real val,
                int  nghost)
{
    mult(val,0,n_comp,nghost);
}

void
MultiFab::mult (Real       val,
                const Box& region,
                int        nghost)
{
    mult(val,region,0,n_comp,nghost);
}

void
MultiFab::invert (Real numerator,
                  int  nghost)
{
    invert(numerator,0,n_comp,nghost);
}

void
MultiFab::invert (Real       numerator,
                  const Box& region,
                  int        nghost)
{
    invert(numerator,region,0,n_comp,nghost);
}

void
MultiFab::negate (int nghost)
{
    negate(0,n_comp,nghost);
}

void
MultiFab::negate (const Box& region,
                  int        nghost)
{
    negate(region,0,n_comp,nghost);
}

void
MultiFab::Initialize ()
{
    if (initialized) return;
    initialized = true;

    amrex::ExecOnFinalize(MultiFab::Finalize);

#ifdef BL_MEM_PROFILING
    MemProfiler::add("MultiFab", std::function<MemProfiler::NBuildsInfo()>
		     ([] () -> MemProfiler::NBuildsInfo {
			 return {num_multifabs, num_multifabs_hwm};
		     }));
#endif
}

void
MultiFab::Finalize ()
{
    initialized = false;
}

MultiFab::MultiFab ()
{
#ifdef BL_MEM_PROFILING
    ++num_multifabs;
    num_multifabs_hwm = std::max(num_multifabs_hwm, num_multifabs);
#endif
}

MultiFab::MultiFab (const BoxArray&            bxs,
                    const DistributionMapping& dm,
                    int                        ncomp,
                    int                        ngrow,
		    const MFInfo&              info,
                    const FabFactory<FArrayBox>& factory)
    : MultiFab(bxs,dm,ncomp,IntVect(ngrow),info,factory)
{}

MultiFab::MultiFab (const BoxArray&            bxs,
                    const DistributionMapping& dm,
                    int                        ncomp,
                    const IntVect&             ngrow,
		    const MFInfo&              info,
                    const FabFactory<FArrayBox>& factory)
    :
    FabArray<FArrayBox>(bxs,dm,ncomp,ngrow,info,factory)
{
    if (SharedMemory() && info.alloc) initVal();  // else already done in FArrayBox
#ifdef BL_MEM_PROFILING
    ++num_multifabs;
    num_multifabs_hwm = std::max(num_multifabs_hwm, num_multifabs);
#endif
}

MultiFab::MultiFab (const MultiFab& rhs, MakeType maketype, int scomp, int ncomp)
    :
    FabArray<FArrayBox>(rhs, maketype, scomp, ncomp)
{
#ifdef BL_MEM_PROFILING
    ++num_multifabs;
    num_multifabs_hwm = std::max(num_multifabs_hwm, num_multifabs);
#endif
}

MultiFab::MultiFab (const BoxArray& ba, const DistributionMapping& dm, int ncomp, int ngrow,
                    const Vector<Real*>& p)
    : MultiFab(ba, dm, ncomp, IntVect(ngrow), p)
{}

MultiFab::MultiFab (const BoxArray& ba, const DistributionMapping& dm, int ncomp, const IntVect& ngrow,
                    const Vector<Real*>& p)
    :
    FabArray<FArrayBox>(ba, dm, ncomp, ngrow, p)
{
#ifdef BL_MEM_PROFILING
    ++num_multifabs;
    num_multifabs_hwm = std::max(num_multifabs_hwm, num_multifabs);
#endif
}

MultiFab::MultiFab (MultiFab&& rhs) noexcept
    : FabArray<FArrayBox>(std::move(rhs))
{
#ifdef BL_MEM_PROFILING
    ++num_multifabs;
    num_multifabs_hwm = std::max(num_multifabs_hwm, num_multifabs);
#endif
}

MultiFab::~MultiFab()
{
#ifdef BL_MEM_PROFILING
    --num_multifabs;
#endif
}

void
MultiFab::operator= (const Real& r)
{
    setVal(r);
}

void
MultiFab::define (const BoxArray&            bxs,
                  const DistributionMapping& dm,
                  int                        nvar,
                  int                        ngrow,
		  const MFInfo&              info,
                  const FabFactory<FArrayBox>& factory)
{
    define(bxs, dm, nvar, IntVect(ngrow), info, factory);
    if (SharedMemory() && info.alloc) initVal();  // else already done in FArrayBox
}

void
MultiFab::define (const BoxArray&            bxs,
                  const DistributionMapping& dm,
                  int                        nvar,
                  const IntVect&             ngrow,
		  const MFInfo&              info,
                  const FabFactory<FArrayBox>& factory)
{
    this->FabArray<FArrayBox>::define(bxs,dm,nvar,ngrow,info,factory);
    if (SharedMemory() && info.alloc) initVal();  // else already done in FArrayBox
}

void
MultiFab::initVal ()
{
#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this); mfi.isValid(); ++mfi)
    {
	(*this)[mfi].initVal();
    }
}

bool 
MultiFab::contains_nan (int scomp,
                        int ncomp,
                        int ngrow,
			bool local) const
{
    BL_ASSERT(scomp >= 0);
    BL_ASSERT(scomp + ncomp <= nComp());
    BL_ASSERT(ncomp >  0 && ncomp <= nComp());
    BL_ASSERT(ngrow >= 0 && ngrow <= nGrow());

    bool r = false;

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel reduction(|:r)
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
	const Box& bx = mfi.growntilebox(ngrow);
	
	if (this->FabArray<FArrayBox>::get(mfi).contains_nan(bx,scomp,ncomp))
	    r = true;
    }

    if (!local)
	ParallelAllReduce::Or(r, ParallelContext::CommunicatorSub());

    return r;
}

bool 
MultiFab::contains_nan (bool local) const
{
    return contains_nan(0,nComp(),nGrow(),local);
}

bool 
MultiFab::contains_inf (int scomp,
                        int ncomp,
                        int ngrow,
			bool local) const
{
    BL_ASSERT(scomp >= 0);
    BL_ASSERT(scomp + ncomp <= nComp());
    BL_ASSERT(ncomp >  0 && ncomp <= nComp());
    BL_ASSERT(ngrow >= 0 && ngrow <= nGrow());

    bool r = false;

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel reduction(|:r)
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
	const Box& bx = mfi.growntilebox(ngrow);
	
	if (this->FabArray<FArrayBox>::get(mfi).contains_inf(bx,scomp,ncomp))
	    r = true;
    }

    if (!local)
	ParallelAllReduce::Or(r, ParallelContext::CommunicatorSub());

    return r;
}

bool 
MultiFab::contains_inf (bool local) const
{
    return contains_inf(0,nComp(),nGrow(),local);
}

bool 
MultiFab::is_nodal () const
{
    return boxArray().ixType().nodeCentered();
}

bool 
MultiFab::is_nodal (int dir) const
{
    return boxArray().ixType().nodeCentered(dir);
}

bool 
MultiFab::is_cell_centered () const
{
    return boxArray().ixType().cellCentered();
}

Real
MultiFab::min (int comp,
               int nghost,
	       bool local) const
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());

    Real mn = std::numeric_limits<Real>::max();

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel reduction(min:mn)
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
	const Box& bx = mfi.growntilebox(nghost);
	mn = std::min(mn, get(mfi).min(bx,comp));
    }

    if (!local)
	ParallelAllReduce::Min(mn, ParallelContext::CommunicatorSub());

    return mn;
}

Real
MultiFab::min (const Box& region,
               int        comp,
               int        nghost,
	       bool       local) const
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());

    Real mn = std::numeric_limits<Real>::max();

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel reduction(min:mn)
#endif
    for ( MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
	const Box& b = mfi.growntilebox(nghost) & region;
	
	if (b.ok())
	    mn = std::min(mn, get(mfi).min(b,comp));
    }

    if (!local)
	ParallelAllReduce::Min(mn, ParallelContext::CommunicatorSub());

    return mn;
}

Real
MultiFab::max (int comp,
               int nghost,
	       bool local) const
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());

    Real mx = -std::numeric_limits<Real>::max();

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel reduction(max:mx)
#endif
    for ( MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
	const Box& bx = mfi.growntilebox(nghost);
	mx = std::max(mx, get(mfi).max(bx,comp));
    }

    if (!local)
	ParallelAllReduce::Max(mx, ParallelContext::CommunicatorSub());

    return mx;
}

Real
MultiFab::max (const Box& region,
               int        comp,
               int        nghost,
	       bool       local) const
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());

    Real mx = -std::numeric_limits<Real>::max();

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel reduction(max:mx)
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
	const Box& b = mfi.growntilebox(nghost) & region;

	if (b.ok())
	    mx = std::max(mx, get(mfi).max(b,comp));
    }
	
    if (!local)
	ParallelAllReduce::Max(mx, ParallelContext::CommunicatorSub());

    return mx;
}

IntVect
MultiFab::minIndex (int comp,
                    int nghost) const
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());

    Real mn = std::numeric_limits<Real>::max();
    IntVect loc;

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    {
	Real priv_mn = std::numeric_limits<Real>::max();
	IntVect priv_loc;

	for (MFIter mfi(*this); mfi.isValid(); ++mfi)
	{
	    const Box& bx = amrex::grow(mfi.validbox(),nghost);
	    const Real lmn = get(mfi).min(bx,comp);

	    if (lmn < priv_mn)
	    {
		priv_mn  = lmn;
		priv_loc = get(mfi).minIndex(bx,comp);
	    }
	}
#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp critical (multifab_minindex)
#endif
	{
	    if (priv_mn < mn) {
		mn = priv_mn;
		loc = priv_loc;
	    }
	}
    }

    const int NProcs = ParallelContext::NProcsSub();
    if (NProcs > 1)
    {
        Vector<Real> mns(NProcs);
        Vector<int>  locs(NProcs * AMREX_SPACEDIM);

        auto comm = ParallelContext::CommunicatorSub();
        ParallelAllGather::AllGather(mn, mns.dataPtr(), comm);
        BL_ASSERT(sizeof(IntVect) == sizeof(int)*AMREX_SPACEDIM);
        ParallelAllGather::AllGather(loc.getVect(), AMREX_SPACEDIM, locs.dataPtr(), comm);

        mn  = mns[0];
        loc = IntVect(AMREX_D_DECL(locs[0],locs[1],locs[2]));
        for (int i = 1; i < NProcs; i++)
        {
            if (mns[i] < mn)
            {
                mn = mns[i];
                const int j = AMREX_SPACEDIM * i;
                loc = IntVect(AMREX_D_DECL(locs[j+0],locs[j+1],locs[j+2]));
            }
        }
    }

    return loc;
}

IntVect
MultiFab::maxIndex (int comp,
                    int nghost) const
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());

    Real mx = std::numeric_limits<Real>::lowest();
    IntVect loc;

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    {
	Real priv_mx = std::numeric_limits<Real>::lowest();
	IntVect priv_loc;

	for (MFIter mfi(*this); mfi.isValid(); ++mfi)
	{
	    const Box& bx = amrex::grow(mfi.validbox(),nghost);
	    const Real lmx = get(mfi).max(bx,comp);

	    if (lmx > priv_mx)
	    {
		priv_mx  = lmx;
		priv_loc = get(mfi).maxIndex(bx,comp);
	    }
	}
#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp critical (multifab_maxindex)
#endif
	{
	    if (priv_mx > mx) {
		mx = priv_mx;
		loc = priv_loc;
	    }
	}
    }

    const int NProcs = ParallelContext::NProcsSub();
    if (NProcs > 1)
    {
        Vector<Real> mxs(NProcs);
        Vector<int>  locs(NProcs * AMREX_SPACEDIM);

        auto comm = ParallelContext::CommunicatorSub();
        ParallelAllGather::AllGather(mx, mxs.dataPtr(), comm);
        BL_ASSERT(sizeof(IntVect) == sizeof(int)*AMREX_SPACEDIM);
        ParallelAllGather::AllGather(loc.getVect(), AMREX_SPACEDIM, locs.dataPtr(), comm);

        mx  = mxs[0];
        loc = IntVect(AMREX_D_DECL(locs[0],locs[1],locs[2]));
        for (int i = 1; i < NProcs; i++)
        {
            if (mxs[i] > mx)
            {
                mx = mxs[i];

                const int j = AMREX_SPACEDIM * i;

                loc = IntVect(AMREX_D_DECL(locs[j+0],locs[j+1],locs[j+2]));
            }
        }
    }

    return loc;
}

#if !defined(BL_NO_FORT)
Real
MultiFab::norm0 (const iMultiFab& mask, int comp, int nghost, bool local) const
{
    Real nm0 = -std::numeric_limits<Real>::max();

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel reduction(max:nm0)
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
	nm0 = std::max(nm0, get(mfi).norminfmask(mfi.growntilebox(nghost), mask[mfi], comp, 1));
    }

    if (!local)	ParallelAllReduce::Max(nm0, ParallelContext::CommunicatorSub());

    return nm0;
}
#endif

Real
MultiFab::norm0 (int comp, const BoxArray& ba, int nghost, bool local) const
{
    Real nm0 = -std::numeric_limits<Real>::max();

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel reduction(max:nm0)
#endif
    {
	std::vector< std::pair<int,Box> > isects;

	for (MFIter mfi(*this); mfi.isValid(); ++mfi)
	{
	    ba.intersections(amrex::grow(mfi.validbox(),nghost),isects);

	    for (int i = 0, N = isects.size(); i < N; i++)
	    {
		nm0 = std::max(nm0, get(mfi).norm(isects[i].second, 0, comp, 1));
	    }
	}
    }
 
    if (!local)
	ParallelAllReduce::Max(nm0, ParallelContext::CommunicatorSub());
 
    return nm0;
}

Real
MultiFab::norm0 (int comp, int nghost, bool local) const
{
    Real nm0 = -std::numeric_limits<Real>::max();

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel reduction(max:nm0)
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
	nm0 = std::max(nm0, get(mfi).norm(mfi.growntilebox(nghost), 0, comp, 1));
    }

    if (!local)
	ParallelAllReduce::Max(nm0, ParallelContext::CommunicatorSub());

    return nm0;
}

Vector<Real>
MultiFab::norm0 (const Vector<int>& comps, int nghost, bool local) const
{
    int n = comps.size();
    const Real rmax = std::numeric_limits<Real>::max();
    Vector<Real> nm0(n, -rmax);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
    int nthreads = omp_get_max_threads();
#else
    int nthreads = 1;
#endif
    Vector<Vector<Real> > priv_nm0(nthreads, Vector<Real>(n, -rmax));

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    {
#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
	int tid = omp_get_thread_num();
#else
	int tid = 0;
#endif
	for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
	{
            for (int i=0; i<n; i++) {
	        priv_nm0[tid][i] = std::max(priv_nm0[tid][i], 
					    get(mfi).norm(mfi.growntilebox(nghost), 0, comps[i], 1));
            }
        }
#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp barrier
#pragma omp for
#endif
	for (int i=0; i<n; i++) {
            for (int it=0; it<nthreads; it++) {
	        nm0[i] = std::max(priv_nm0[it][i], nm0[i]);
            }	    
	}
    }

    if (!local)
	ParallelAllReduce::Max(nm0.dataPtr(), n, ParallelContext::CommunicatorSub());

    return nm0;
}

#if !defined(BL_NO_FORT)

Real
MultiFab::norm2 (int comp) const
{
    BL_ASSERT(ixType().cellCentered());

    // Dot expects two MultiDabs. Make a copy to avoid aliasing.
    MultiFab tmpmf(boxArray(), DistributionMap(), 1, 0, MFInfo(), Factory());
    MultiFab::Copy(tmpmf, *this, comp, 0, 1, 0);

    Real nm2 = MultiFab::Dot(*this, comp, tmpmf, 0, 1, 0);
    nm2 = std::sqrt(nm2);
    return nm2;
}

Real
MultiFab::norm2 (int comp, const Periodicity& period) const
{
    MultiFab tmpmf(boxArray(), DistributionMap(), 1, 0, MFInfo(), Factory());
    MultiFab::Copy(tmpmf, *this, comp, 0, 1, 0);

    auto mask = OverlapMask(period);
    MultiFab::Divide(tmpmf, *mask, 0, 0, 1, 0);

    Real nm2 = MultiFab::Dot(*this, comp, tmpmf, 0, 1, 0);
    nm2 = std::sqrt(nm2);
    return nm2;
}

Vector<Real>
MultiFab::norm2 (const Vector<int>& comps) const
{
    BL_ASSERT(ixType().cellCentered());

    int n = comps.size();
    Vector<Real> nm2(n, 0.e0);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
    int nthreads = omp_get_max_threads();
#else
    int nthreads = 1;
#endif
    Vector<Vector<Real> > priv_nm2(nthreads, Vector<Real>(n, 0.0));

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    {
#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
	int tid = omp_get_thread_num();
#else
	int tid = 0;
#endif

	for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
	{
	    const Box& bx = mfi.tilebox();
	    const FArrayBox& fab = get(mfi);
            for (int i=0; i<n; i++) {
		priv_nm2[tid][i] += fab.dot(bx,comps[i],fab,bx,comps[i]);
            }
        }
#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp barrier
#pragma omp for
#endif
	for (int i=0; i<n; i++) {
	    for (int it=1; it<nthreads; it++) {
		priv_nm2[0][i] += priv_nm2[it][i];
	    }
	}
    }

    ParallelAllReduce::Sum(&priv_nm2[0][0], n, ParallelContext::CommunicatorSub());

    for (int i=0; i<n; i++) {
	nm2[i] = std::sqrt(priv_nm2[0][i]);
    }

    return nm2;
}

#endif

Real
MultiFab::norm1 (int comp, const Periodicity& period) const
{
    MultiFab tmpmf(boxArray(), DistributionMap(), 1, 0, MFInfo(), Factory());
    MultiFab::Copy(tmpmf, *this, comp, 0, 1, 0);

    auto mask = OverlapMask(period);
    MultiFab::Divide(tmpmf, *mask, 0, 0, 1, 0);

    return tmpmf.norm1(0, 0);
}

Real
MultiFab::norm1 (int comp, int ngrow, bool local) const
{
    
    Real nm1 = 0.e0;

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel if (!system::regtest_reduction) reduction(+:nm1)
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        nm1 += get(mfi).norm(mfi.growntilebox(ngrow), 1, comp, 1);
    }

    if (!local)
	ParallelAllReduce::Sum(nm1, ParallelContext::CommunicatorSub());

    return nm1;
}

Vector<Real>
MultiFab::norm1 (const Vector<int>& comps, int ngrow, bool local) const
{
    BL_ASSERT(ixType().cellCentered());

    int n = comps.size();
    Vector<Real> nm1(n, 0.e0);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
    int nthreads = omp_get_max_threads();
#else
    int nthreads = 1;
#endif
    Vector<Vector<Real> > priv_nm1(nthreads, Vector<Real>(n, 0.0));

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    {
#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
	int tid = omp_get_thread_num();
#else
	int tid = 0;
#endif
	for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
	{
            const Box& b = mfi.growntilebox(ngrow);
            for (int i=0; i<n; i++) {
                priv_nm1[tid][i] += get(mfi).norm(b, 1, comps[i], 1);
	    }
        }
#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp barrier
#pragma omp for
#endif
	for (int i=0; i<n; i++) {
	    for (int it=0; it<nthreads; it++) {
		nm1[i] += priv_nm1[it][i];
	    }
	}
    }

    if (!local)
	ParallelAllReduce::Sum(nm1.dataPtr(), n, ParallelContext::CommunicatorSub());

    return nm1;
}

Real
MultiFab::sum (int comp, bool local) const
{
    Real sm = 0.e0;

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel if (!system::regtest_reduction) reduction(+:sm)
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        sm += get(mfi).sum(mfi.tilebox(), comp, 1);
    }

    if (!local)
        ParallelAllReduce::Sum(sm, ParallelContext::CommunicatorSub());

    return sm;
}

void
MultiFab::minus (const MultiFab& mf,
                 int             strt_comp,
                 int             num_comp,
                 int             nghost)
{
    BL_ASSERT(boxarray == mf.boxarray);
    BL_ASSERT(strt_comp >= 0);
    BL_ASSERT(num_comp > 0);
    BL_ASSERT(strt_comp + num_comp - 1 < n_comp && strt_comp + num_comp - 1 < mf.n_comp);
    BL_ASSERT(nghost <= n_grow.min() && nghost <= mf.n_grow.min());

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);

        get(mfi).minus(mf[mfi], bx, strt_comp, strt_comp, num_comp);
    }
}

void
MultiFab::divide (const MultiFab& mf,
		  int             strt_comp,
		  int             num_comp,
		  int             nghost)
{
    BL_ASSERT(boxarray == mf.boxarray);
    BL_ASSERT(strt_comp >= 0);
    BL_ASSERT(num_comp > 0);
    BL_ASSERT(strt_comp + num_comp - 1 < n_comp && strt_comp + num_comp - 1 < mf.n_comp);
    BL_ASSERT(nghost <= n_grow.min() && nghost <= mf.n_grow.min());

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);

        get(mfi).divide(mf[mfi], bx, strt_comp, strt_comp, num_comp);
    }
}

void
MultiFab::plus (Real val,
                int  comp,
                int  num_comp,
                int  nghost)
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());
    BL_ASSERT(comp+num_comp <= n_comp);
    BL_ASSERT(num_comp > 0);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        get(mfi).plus(val,mfi.growntilebox(nghost),comp,num_comp);
    }
}

void
MultiFab::plus (Real       val,
                const Box& region,
                int        comp,
                int        num_comp,
                int        nghost)
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());
    BL_ASSERT(comp+num_comp <= n_comp);
    BL_ASSERT(num_comp > 0);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        const Box& b = mfi.growntilebox(nghost) & region;

        if (b.ok())
            get(mfi).plus(val,b,comp,num_comp);
    }
}

void
MultiFab::plus (const MultiFab& mf,
                int             strt_comp,
                int             num_comp,
                int             nghost)
{
    BL_ASSERT(boxarray == mf.boxarray);
    BL_ASSERT(strt_comp >= 0);
    BL_ASSERT(num_comp > 0);
    BL_ASSERT(strt_comp + num_comp - 1 < n_comp && strt_comp + num_comp - 1 < mf.n_comp);
    BL_ASSERT(nghost <= n_grow.min() && nghost <= mf.n_grow.min());

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(nghost);

        get(mfi).plus(mf[mfi], bx, strt_comp, strt_comp, num_comp);
    }
}

void
MultiFab::mult (Real val,
                int  comp,
                int  num_comp,
                int  nghost)
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());
    BL_ASSERT(comp+num_comp <= n_comp);
    BL_ASSERT(num_comp > 0);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        get(mfi).mult(val, mfi.growntilebox(nghost), comp, num_comp);
    }
}

void
MultiFab::mult (Real       val,
                const Box& region,
                int        comp,
                int        num_comp,
                int        nghost)
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());
    BL_ASSERT(comp+num_comp <= n_comp);
    BL_ASSERT(num_comp > 0);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        const Box& b = mfi.growntilebox(nghost) & region;

        if (b.ok())
            get(mfi).mult(val, b, comp, num_comp);
    }
}

void
MultiFab::invert (Real numerator,
                  int  comp,
                  int  num_comp,
                  int  nghost)
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());
    BL_ASSERT(comp+num_comp <= n_comp);
    BL_ASSERT(num_comp > 0);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        get(mfi).invert(numerator, mfi.growntilebox(nghost), comp, num_comp);
    }
}

void
MultiFab::invert (Real       numerator,
                  const Box& region,
                  int        comp,
                  int        num_comp,
                  int        nghost)
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());
    BL_ASSERT(comp+num_comp <= n_comp);
    BL_ASSERT(num_comp > 0);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        const Box& b = mfi.growntilebox(nghost) & region;

        if (b.ok())
            get(mfi).invert(numerator,b,comp,num_comp);
    }
}

void
MultiFab::negate (int comp,
                  int num_comp,
                  int nghost)
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());
    BL_ASSERT(comp+num_comp <= n_comp);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        get(mfi).negate(mfi.growntilebox(nghost), comp, num_comp);
    }
}

void
MultiFab::negate (const Box& region,
                  int        comp,
                  int        num_comp,
                  int        nghost)
{
    BL_ASSERT(nghost >= 0 && nghost <= n_grow.min());
    BL_ASSERT(comp+num_comp <= n_comp);

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        const Box& b = mfi.growntilebox(nghost) & region;

        if (b.ok())
            get(mfi).negate(b,comp,num_comp);
    }
}

void
MultiFab::SumBoundary (int scomp, int ncomp, const Periodicity& period)
{
    BL_PROFILE("MultiFab::SumBoundary()");

    if ( n_grow == IntVect::TheZeroVector() && boxArray().ixType().cellCentered()) return;

    if (boxArray().ixType().cellCentered()) {
	// Self copy is safe only for cell-centered MultiFab
	this->copy(*this,scomp,scomp,ncomp,n_grow,IntVect::TheZeroVector(),period,FabArrayBase::ADD);
    } else {
	MultiFab tmp(boxArray(), DistributionMap(), ncomp, n_grow, MFInfo(), Factory());
	MultiFab::Copy(tmp, *this, scomp, 0, ncomp, n_grow);
	this->setVal(0.0, scomp, ncomp, 0);
	this->copy(tmp,0,scomp,ncomp,n_grow,IntVect::TheZeroVector(),period,FabArrayBase::ADD);
    }
}

void
MultiFab::SumBoundary (const Periodicity& period)
{
    SumBoundary(0, n_comp, period);
}

std::unique_ptr<MultiFab>
MultiFab::OverlapMask (const Periodicity& period) const
{
    const BoxArray& ba = boxArray();
    const DistributionMapping& dm = DistributionMap();

    std::unique_ptr<MultiFab> p{new MultiFab(ba,dm,1,0, MFInfo(), Factory())};
    p->setVal(0.0);

    const std::vector<IntVect>& pshifts = period.shiftIntVect();

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    {
        std::vector< std::pair<int,Box> > isects;
        
        for (MFIter mfi(*p); mfi.isValid(); ++mfi)
        {
            FArrayBox& fab = (*p)[mfi];
            const Box& bx = fab.box();
            for (const auto& iv : pshifts)
            {
                ba.intersections(bx+iv, isects);                    
                for (const auto& is : isects)
                {
                    fab.plus(1.0, is.second-iv);
                }
            }
        }
    }
    
    return p;
}

std::unique_ptr<iMultiFab>
MultiFab::OwnerMask (const Periodicity& period) const
{
    const BoxArray& ba = boxArray();
    const DistributionMapping& dm = DistributionMap();

    const int owner = 1;
    const int nonowner = 0;

    std::unique_ptr<iMultiFab> p{new iMultiFab(ba,dm,1,0, MFInfo(),
                                               DefaultFabFactory<IArrayBox>())};
    p->setVal(owner);

    const std::vector<IntVect>& pshifts = period.shiftIntVect();

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    {
        std::vector< std::pair<int,Box> > isects;
        
        for (MFIter mfi(*p); mfi.isValid(); ++mfi)
        {
            IArrayBox& fab = (*p)[mfi];
            const Box& bx = fab.box();
            const int i = mfi.index();
            for (const auto& iv : pshifts)
            {
                ba.intersections(bx+iv, isects);                    
                for (const auto& is : isects)
                {
                    const int oi = is.first;
                    const Box& obx = is.second;
                    if ((oi < i) || (oi == i && iv < IntVect::TheZeroVector())) {
                        fab.setVal(nonowner, obx-iv, 0, 1);
                    }
                }
            }
        }
    }

    return p;
}

void
MultiFab::AverageSync (const Periodicity& period)
{
    if (ixType().cellCentered()) return;
    auto wgt = this->OverlapMask(period);
    wgt->invert(1.0, 0, 1);
    this->WeightedSync(*wgt, period);
}

void
MultiFab::WeightedSync (const MultiFab& wgt, const Periodicity& period)
{
    if (ixType().cellCentered()) return;
    
    const int ncomp = nComp();
    for (int comp = 0; comp < ncomp; ++comp)
    {
        MultiFab::Multiply(*this, wgt, 0, comp, 1, 0);
    }
    
    MultiFab tmpmf(boxArray(), DistributionMap(), ncomp, 0, MFInfo(), Factory());
    tmpmf.setVal(0.0);
    tmpmf.ParallelCopy(*this, period, FabArrayBase::ADD);

    MultiFab::Copy(*this, tmpmf, 0, 0, ncomp, 0);
}

#if !defined(BL_NO_FORT)

void
MultiFab::OverrideSync (const Periodicity& period)
{
    if (ixType().cellCentered()) return;
    auto msk = this->OwnerMask(period);
    this->OverrideSync(*msk, period);
}

void
MultiFab::OverrideSync (const iMultiFab& msk, const Periodicity& period)
{
    if (ixType().cellCentered()) return;
    
    const int ncomp = nComp();

#if defined(_OPENMP) && !defined(AMREX_USE_CUDA)
#pragma omp parallel
#endif
    for (MFIter mfi(*this,true); mfi.isValid(); ++mfi)
    {
        FArrayBox& fab = (*this)[mfi];
        const IArrayBox& ifab = msk[mfi];
        const Box& bx = mfi.tilebox();
        amrex_fab_setval_ifnot (BL_TO_FORTRAN_BOX(bx),
                                BL_TO_FORTRAN_FAB(fab),
                                BL_TO_FORTRAN_ANYD(ifab),
                                0.0);
    }
    
    MultiFab tmpmf(boxArray(), DistributionMap(), ncomp, 0, MFInfo(), Factory());
    tmpmf.setVal(0.0);
    tmpmf.ParallelCopy(*this, period, FabArrayBase::ADD);

    MultiFab::Copy(*this, tmpmf, 0, 0, ncomp, 0);
}

#endif

}
