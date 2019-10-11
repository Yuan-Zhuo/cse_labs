// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include "lock_client.h"
#include "lock_protocol.h"
#include "rpc.h"

class lock_server {
  class lock_ent {
   public:
    lock_ent() { pthread_mutex_init(&mutex, NULL); }
    ~lock_ent() { pthread_mutex_destroy(&mutex); }
    pthread_mutex_t mutex;
  };
  typedef std::map<lock_protocol::lockid_t, lock_ent> lock_map;

 private:
  static lock_map create_map() {
    lock_map map;
    return map;
  }

 protected:
  int nacquire;
  static lock_map map;

 public:
  lock_server();
  ~lock_server(){};
  lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &);
};

#endif
