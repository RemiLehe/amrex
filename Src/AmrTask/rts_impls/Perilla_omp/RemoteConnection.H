#ifndef P_REMOTECONNECTION_H
#define P_REMOTECONNECTION_H

#include <omp.h>
#include <AMReX_Box.H>
#include <PackageQueue.H>

class RemoteCommDescriptor
{
public:
  int ns, lns;   // ! Source box in layout
  int nd, lnd;   //! Destination box in layout
  int r_gid;
  int r_grids;
  int sz, pr;
  Box sbx;     // ! Sub-box for this copy
  Box dbx;     // ! Sub-box for this copy
  PackageQueue pQueue;  // !store incoming or outgoing messages, both fab and the runtime can access this queue
  PackageQueue recycleQueue; //!store used messages, only fab can access this queue, no lock is required
  int cnt;
  RemoteCommDescriptor() :
    ns(-1), lns(-1),
    nd(-1), lnd(-1),
    sz(0), pr(0),
    cnt(0), r_gid(0),
    r_grids(0)
  {}
};

class TransDescriptor
{
  int sz, pv, pr;
};
  
class RemoteConnection  
{
public:
  int nsnd;
  int nrcv;
  bool remotePushReady;
  bool remotePullDone;
  int nrp, nsp;
  omp_lock_t *sndLock, *rcvLock, *ghostLock;
  int firingRuleCnt;
  RemoteCommDescriptor *snd;
  RemoteCommDescriptor *rcv;
  TransDescriptor *str;
  TransDescriptor *rtr;
  RemoteConnection() :
    nsnd(0),
    nrcv(0),
    remotePushReady(false),
    remotePullDone(false),
    nrp(0), nsp(0),
    firingRuleCnt(0),
    snd(NULL), rcv(NULL),
    str(NULL), rtr(NULL)
  {
    sndLock = new omp_lock_t;
    rcvLock = new omp_lock_t;
    ghostLock = new omp_lock_t;
    omp_init_lock(sndLock);
    omp_init_lock(rcvLock);
    omp_init_lock(ghostLock);
  }

  ~RemoteConnection()
  {
    if(!snd)
      delete[] snd;
    if(!rcv)
      delete[] rcv;
  }
};


#endif
