
//
// $Id: DistributionMapping.cpp,v 1.44 2000-10-19 18:04:17 lijewski Exp $
//

#include <DistributionMapping.H>
#include <List.H>
#include <ParallelDescriptor.H>
#include <ParmParse.H>
#include <RunStats.H>

#ifdef BL_USE_NEW_HFILES
#include <iostream>
#include <cstdlib>
#include <list>
#include <vector>
#include <queue>
#include <algorithm>
#include <numeric>
using namespace std;
#else
#include <iostream.h>
#include <stdlib.h>
#include <vector.h>
#include <list.h>
#if defined(BL_OLD_STL)
#include <stack.h>
#include <algo.h>
#else
#include <queue.h>
#include <algorithm.h>
#include <numeric.h>
#endif
#endif /*BL_USE_NEW_HFILES*/

#ifdef BL_NAMESPACE
namespace BL_NAMESPACE
{
#endif

//
// Everyone uses the same Strategy -- defaults to KNAPSACK.
//
DistributionMapping::Strategy
DistributionMapping::m_Strategy = DistributionMapping::KNAPSACK;

DistributionMapping::PVMF
DistributionMapping::m_BuildMap = &DistributionMapping::KnapSackProcessorMap;

void
DistributionMapping::strategy (DistributionMapping::Strategy how)
{
    DistributionMapping::m_Strategy = how;

    switch (how)
    {
    case ROUNDROBIN:
        m_BuildMap = &DistributionMapping::RoundRobinProcessorMap;
        break;
    case KNAPSACK:
        m_BuildMap = &DistributionMapping::KnapSackProcessorMap;
        break;
    default:
        BoxLib::Error("Bad DistributionMapping::Strategy");
    }
}

//
// We start out uninitialized.
//
bool DistributionMapping::m_Initialized = false;

bool
DistributionMapping::operator== (const DistributionMapping& rhs) const
{
    return m_procmap == rhs.m_procmap;
}

bool
DistributionMapping::operator!= (const DistributionMapping& rhs) const
{
    return !operator==(rhs);
}

void
DistributionMapping::init ()
{
    DistributionMapping::m_Initialized = true;
        
    ParmParse pp("DistributionMapping");

    aString theStrategy;

    if (pp.query("strategy", theStrategy))
    {
        DistributionMapping::Strategy how = ROUNDROBIN;

        if (theStrategy == "ROUNDROBIN")
        {
            how = ROUNDROBIN;
        }
        else if (theStrategy == "KNAPSACK")
        {
            how = KNAPSACK;
        }
        else
        {
            aString msg("Unknown strategy: ");
            msg += theStrategy;
            BoxLib::Warning(msg.c_str());
        }

        strategy(how);
    }
}

//
// Our cache of processor maps.
//
vector< Array<int> > DistributionMapping::m_Cache;

bool
DistributionMapping::GetMap (const BoxArray& boxes)
{
    const int N = boxes.length();

    BL_ASSERT(m_procmap.length() == N+1);
    //
    // Search from back to front ...
    //
    for (int i = m_Cache.size() - 1; i >= 0; i--)
    {
        if (m_Cache[i].length() == N+1)
        {
            const Array<int>& cached_procmap = m_Cache[i];

            for (int i = 0; i <= N; i++)
            {
                m_procmap[i] = cached_procmap[i];
            }

            BL_ASSERT(m_procmap[N] == ParallelDescriptor::MyProc());

            return true;
        }
    }

    return false;
}

DistributionMapping::DistributionMapping ()
{
    if (!m_Initialized)
        DistributionMapping::init();
}

DistributionMapping::DistributionMapping (const BoxArray& boxes)
    :
    m_procmap(boxes.length()+1)
{
    if (!m_Initialized)
        DistributionMapping::init();

    define(boxes);
}

DistributionMapping::DistributionMapping (const DistributionMapping& d1,
                                          const DistributionMapping& d2)
{
    if (!m_Initialized)
        DistributionMapping::init();

    const Array<int>& pmap_1 = d1.ProcessorMap();
    const Array<int>& pmap_2 = d2.ProcessorMap();

    const int L1 = pmap_1.length() - 1; // Length not including sentinel.
    const int L2 = pmap_2.length() - 1; // Length not including sentinel.

    m_procmap.resize(L1+L2+1);

    const int NProcs = ParallelDescriptor::NProcs();

    for (int i = 0; i < L1; i++)
        m_procmap[i] = pmap_1[i];

    for (int i = L1, j = 0; j < L2; i++, j++)
        m_procmap[i] = pmap_2[j];
    //
    // Set sentinel equal to our processor number.
    //
    m_procmap[m_procmap.length()-1] = ParallelDescriptor::MyProc();
}

void
DistributionMapping::define (const BoxArray& boxes)
{
    if (!(m_procmap.length() == boxes.length()+1))
    {
        m_procmap.resize(boxes.length()+1);
    }

    if (DistributionMapping::m_Strategy == DistributionMapping::ROUNDROBIN)
    {
        //
        // Don't bother with the cache -- just do it :-)
        //
        (this->*m_BuildMap)(boxes);
    }
    else
    {
        if (!GetMap(boxes))
        {
            static RunStats stats("processor_map");

            stats.start();

            (this->*m_BuildMap)(boxes);

#if defined(BL_USE_MPI) && !defined(BL_NO_PROCMAP_CACHE)
            //
            // We always append new processor maps.
            //
            DistributionMapping::m_Cache.push_back(m_procmap);
#endif
            stats.end();
        }
    }
}

DistributionMapping::~DistributionMapping () {}

void
DistributionMapping::FlushCache ()
{
    DistributionMapping::m_Cache.clear();
}

void
DistributionMapping::AddToCache (const DistributionMapping& dm)
{
    bool              doit = true;
    const Array<int>& pmap = dm.ProcessorMap();

    if (pmap.length() > 0)
    {
        BL_ASSERT(pmap[pmap.length()-1] == ParallelDescriptor::MyProc());

        for (int i = 0; i < m_Cache.size(); i++)
        {
            if (pmap.length() == m_Cache[i].length())
            {
                BL_ASSERT(pmap == m_Cache[i]);

                doit = false;
            }
        }

        if (doit)
            m_Cache.push_back(pmap);
    }
}

void
DistributionMapping::RoundRobinProcessorMap (int nboxes)
{
    BL_ASSERT(nboxes > 0);
    m_procmap.resize(nboxes+1);

    const int NProcs = ParallelDescriptor::NProcs();

    for (int i = 0; i < nboxes; i++)
    {
        m_procmap[i] = i % NProcs;
    }
    //
    // Set sentinel equal to our processor number.
    //
    m_procmap[nboxes] = ParallelDescriptor::MyProc();
}

void
DistributionMapping::RoundRobinProcessorMap (const BoxArray& boxes)
{
    BL_ASSERT(boxes.length() > 0);
    BL_ASSERT(m_procmap.length() == boxes.length()+1);

    const int NProcs = ParallelDescriptor::NProcs();

    for (int i = 0; i < boxes.length(); i++)
    {
        m_procmap[i] = i % NProcs;
    }
    //
    // Set sentinel equal to our processor number.
    //
    m_procmap[boxes.length()] = ParallelDescriptor::MyProc();
}

#ifdef BL_USE_MPI

class WeightedBox
{
    int  m_boxid;
    long m_weight;
public:
    WeightedBox () {}
    WeightedBox (int b, int w) : m_boxid(b), m_weight(w) {}
    long weight () const { return m_weight; }
    int  boxid ()  const { return m_boxid;  }

    bool operator< (const WeightedBox& rhs) const
    {
        return weight() > rhs.weight();
    }
};

class WeightedBoxList
{
    list<WeightedBox> m_lb;
    long              m_weight;
public:
    WeightedBoxList() : m_weight(0) {}
    long weight () const
    {
        return m_weight;
    }
    void erase (list<WeightedBox>::iterator& it)
    {
        m_weight -= (*it).weight();
        m_lb.erase(it);
    }
    void push_back (const WeightedBox& bx)
    {
        m_weight += bx.weight();
        m_lb.push_back(bx);
    }
    list<WeightedBox>::const_iterator begin () const { return m_lb.begin(); }
    list<WeightedBox>::iterator begin ()             { return m_lb.begin(); }
    list<WeightedBox>::const_iterator end () const   { return m_lb.end();   }
    list<WeightedBox>::iterator end ()               { return m_lb.end();   }

    bool operator< (const WeightedBoxList& rhs) const
    {
        return weight() > rhs.weight();
    }
};

static
vector< list<int> >
knapsack (const vector<long>& pts)
{
    const int NProcs = ParallelDescriptor::NProcs();
    //
    // Sort balls by size largest first.
    //
    static list<int> empty_list;  // Work-around MSVC++ bug :-(

    vector< list<int> > result(NProcs, empty_list);

    vector<WeightedBox> lb;
    lb.reserve(pts.size());
    for (int i = 0; i < pts.size(); ++i)
    {
        lb.push_back(WeightedBox(i, pts[i]));
    }
    BL_ASSERT(lb.size() == pts.size());
    sort(lb.begin(), lb.end());
    BL_ASSERT(lb.size() == pts.size());
    //
    // For each ball, starting with heaviest, assign ball to the lightest box.
    //
    priority_queue<WeightedBoxList> wblq;
    for (int i  = 0; i < NProcs; ++i)
    {
        wblq.push(WeightedBoxList());
    }
    BL_ASSERT(wblq.size() == NProcs);
    for (int i = 0; i < pts.size(); ++i)
    {
        WeightedBoxList wbl = wblq.top();
        wblq.pop();
        wbl.push_back(lb[i]);
        wblq.push(wbl);
    }
    BL_ASSERT(wblq.size() == NProcs);
    list<WeightedBoxList> wblqg;
    while (!wblq.empty())
    {
        wblqg.push_back(wblq.top());
        wblq.pop();
    }
    BL_ASSERT(wblqg.size() == NProcs);
    wblqg.sort();
    //
    // Compute the max weight and the sum of the weights.
    //
    double max_weight = 0;
    double sum_weight = 0;
    list<WeightedBoxList>::iterator it = wblqg.begin();
    for ( ; it != wblqg.end(); ++it)
    {
        long wgt = (*it).weight();
        sum_weight += wgt;
        max_weight = (wgt > max_weight) ? wgt : max_weight;
    }
top:
    list<WeightedBoxList>::iterator it_top = wblqg.begin();
    list<WeightedBoxList>::iterator it_chk = it_top;
    it_chk++;
    WeightedBoxList wbl_top = *it_top;
    //
    // For each ball in the heaviest box.
    //
    list<WeightedBox>::iterator it_wb = wbl_top.begin();
    for ( ; it_wb != wbl_top.end(); ++it_wb )
    {
        //
        // For each ball not in the heaviest box.
        //
        for ( ; it_chk != wblqg.end(); ++it_chk)
        {
            WeightedBoxList wbl_chk = *it_chk;
            list<WeightedBox>::iterator it_owb = wbl_chk.begin();
            for ( ; it_owb != wbl_chk.end(); ++it_owb)
            {
                //
                // If exchanging these two balls reduces the load balance,
                // then exchange them and go to top.  The way we are doing
                // things, sum_weight cannot change.  So the efficiency will
                // increase if after we switch the two balls *it_wb and
                // *it_owb the max weight is reduced.
                //
                double w_tb = (*it_top).weight() + (*it_owb).weight() - (*it_wb).weight();
                double w_ob = (*it_chk).weight() + (*it_wb).weight() - (*it_owb).weight();
                //
                // If the other ball reduces the weight of the top box when
                // swapped, then it will change the efficiency.
                //
                if (w_tb < (*it_top).weight() && w_ob < (*it_top).weight())
                {
                    //
                    // Adjust the sum weight and the max weight.
                    //
                    WeightedBox wb = *it_wb;
                    WeightedBox owb = *it_owb;
                    wblqg.erase(it_top);
                    wblqg.erase(it_chk);
                    wbl_top.erase(it_wb);
                    wbl_chk.erase(it_owb);
                    wbl_top.push_back(owb);
                    wbl_chk.push_back(wb);
                    list<WeightedBoxList> tmp;
                    tmp.push_back(wbl_top);
                    tmp.push_back(wbl_chk);
                    tmp.sort();
                    wblqg.merge(tmp);
                    max_weight = (*wblqg.begin()).weight();
                    //
                    //efficiency = sum_weight/(NProcs*max_weight);
                    //cout << "Efficiency = " << efficiency << '\n';
                    //cout << "max_weight = " << max_weight << '\n';
                    //
                    goto top;
                }
            }
        }
    }
    //
    // Here I am "load-balanced".
    //
    list<WeightedBoxList>::const_iterator cit = wblqg.begin();
    for (int i = 0; i < NProcs; ++i)
    {
        const WeightedBoxList& wbl = *cit;
        list<WeightedBox>::const_iterator it1 = wbl.begin();
        for ( ; it1 != wbl.end(); ++it1)
        {
            result[i].push_back((*it1).boxid());
        }
        ++cit;
    }

#if 0
    //
    // Output some statistics ...
    //
    if (ParallelDescriptor::IOProcessor())
    {
        long totalwork = accumulate(pts.begin(),pts.end(),0L);

        vector<long> work(NProcs);

        fill(work.begin(),work.end(),0L);

        for (int i = 0; i < NProcs; i++)
        {
            list<int>::const_iterator it = result[i].begin();
            for ( ; it != result[i].end(); ++it)
                work[i] += pts[*it];
        }

        sort(work.begin(),work.end());

        long median = work[NProcs/2];

        if (NProcs%2 == 0)
            median = (median + work[NProcs/2-1])/2;

        double sd = 0;
        for (int i = 0; i < NProcs; i++)
            sd += (work[i]-median) * (work[i]-median);
        sd = sqrt(sd/(NProcs-1));

        cout << "Knapsack Statistics:\n"
             << "\ttotal work: "   << totalwork                      << '\n'
             << "\tminimum work: " << work[0]                        << '\n'
             << "\tmaximum work: " << work[NProcs-1]                 << '\n'
             << "\taverage work: " << totalwork/NProcs               << '\n'
             << "\tmedian work: "  << median                         << '\n'
             << "\tstd. dev.: "    << sd                             << '\n'
             << "\tmin/max: "      << double(work[0])/work[NProcs-1] << '\n'
             << "\tmin/median: "   << double(work[0])/median         << '\n'
             << "\tmax/median: "   << double(work[NProcs-1])/median  << '\n';
    }
#endif

    return result;
}
#endif /*BL_USE_MPI*/

void
DistributionMapping::KnapSackProcessorMap (const vector<long>& pts)
{
    BL_ASSERT(pts.size() > 0);
    m_procmap.resize(pts.size()+1);

    const int NProcs = ParallelDescriptor::NProcs();

    if (pts.size() <= NProcs || NProcs < 2)
    {
        //
        // Might as well just use ROUNDROBIN.
        //
        RoundRobinProcessorMap(pts.size());
    }
    else
    {
#ifdef BL_USE_MPI
        vector< list<int> > vec = knapsack(pts);

        BL_ASSERT(vec.size() == NProcs);

        list<int>::iterator lit;

        for (int i = 0; i < vec.size(); i++)
        {
            for (lit = vec[i].begin(); lit != vec[i].end(); ++lit)
            {
                m_procmap[*lit] = i;
            }
        }
        //
        // Set sentinel equal to our processor number.
        //
        m_procmap[pts.size()] = ParallelDescriptor::MyProc();
#else
        BoxLib::Error("How did this happen?");
#endif
    }
}

void
DistributionMapping::KnapSackProcessorMap (const BoxArray& boxes)
{
    BL_ASSERT(boxes.length() > 0);
    BL_ASSERT(m_procmap.length() == boxes.length()+1);

    const int NProcs = ParallelDescriptor::NProcs();

    if (boxes.length() <= NProcs || NProcs < 2)
    {
        //
        // Might as well just use ROUNDROBIN.
        //
        RoundRobinProcessorMap(boxes);
    }
    else
    {
#ifdef BL_USE_MPI
        vector<long> pts(boxes.length());

        for (int i = 0; i < pts.size(); i++)
        {
            pts[i] = boxes[i].numPts();
        }

        vector< list<int> > vec = knapsack(pts);

        BL_ASSERT(vec.size() == NProcs);

        list<int>::iterator lit;

        for (int i = 0; i < vec.size(); i++)
        {
            for (lit = vec[i].begin(); lit != vec[i].end(); ++lit)
            {
                m_procmap[*lit] = i;
            }
        }
        //
        // Set sentinel equal to our processor number.
        //
        m_procmap[boxes.length()] = ParallelDescriptor::MyProc();
#else
        BoxLib::Error("How did this happen?");
#endif
    }
}

void
DistributionMapping::CacheStats (ostream& os)
{
    os << "The DistributionMapping cache contains "
       << DistributionMapping::m_Cache.size()
       << " Processor Map(s):\n";

    if (!DistributionMapping::m_Cache.empty())
    {
        for (int i = 0; i < m_Cache.size(); i++)
        {
            os << "\tMap #"
               << i
               << " is of length "
               << m_Cache[i].length()
               << '\n';
        }
        os << '\n';
    }
}

ostream&
operator<< (ostream&                   os,
            const DistributionMapping& pmap)
{
    os << "(DistributionMapping" << '\n';

    for (int i = 0; i < pmap.ProcessorMap().length(); i++)
    {
        os << "m_procmap[" << i << "] = " << pmap.ProcessorMap()[i] << '\n';
    }

    os << ')' << '\n';

    if (os.fail())
        BoxLib::Error("operator<<(ostream &, DistributionMapping &) failed");

    return os;
}

#ifdef BL_NAMESPACE
}
#endif

