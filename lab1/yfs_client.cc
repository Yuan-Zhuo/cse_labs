// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include "extent_client.h"

int yfs_client::dir_app(inum parent, const char *name, inum ino) {
  int r = OK;

  struct c_dirent c_ent;
  std::string dir_buf;
  if (ec->get(parent, dir_buf) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  c_ent.inum = ino;
  memcpy(c_ent.name, name, FILENAME_SIZE);
  dir_buf.append((const char *)(&c_ent), sizeof(struct c_dirent));

  if (ec->put(parent, dir_buf) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

release:
  return r;
}

yfs_client::yfs_client() { ec = new extent_client(); }

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst) {
  ec = new extent_client();
  if (ec->put(1, "") != extent_protocol::OK)
    printf("error init root dir\n");  // XYB: init root dir
}

yfs_client::inum yfs_client::n2i(std::string n) {
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string yfs_client::filename(inum inum) {
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool yfs_client::isfile(inum inum) {
  extent_protocol::attr a;

  if (ec->getattr(inum, a) != extent_protocol::OK) {
    printf("error getting attr\n");
    return false;
  }

  if (a.type == extent_protocol::T_FILE) {
    printf("isfile: %lld is a file\n", inum);
    return true;
  }
  printf("isfile: %lld is a dir\n", inum);
  return false;
}
/** Your code here for Lab...
 * You may need to add routines such as
 * readlink, issymlink here to implement symbolic link.
 *
 * */

bool yfs_client::isdir(inum inum) {
  // Oops! is this still correct when you implement symlink?

  // return !isfile(inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    printf("error getting attr\n");
    return false;
  }

  if (a.type == extent_protocol::T_DIR) {
    printf("isfile: %lld is a dir\n", inum);
    return true;
  }
  printf("isfile: %lld is not a dir\n", inum);
  return false;
}

int yfs_client::getfile(inum inum, fileinfo &fin) {
  int r = OK;

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
  return r;
}

int yfs_client::getdir(inum inum, dirinfo &din) {
  int r = OK;

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

release:
  return r;
}

#define EXT_RPC(xx)                                          \
  do {                                                       \
    if ((xx) != extent_protocol::OK) {                       \
      printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
      r = IOERR;                                             \
      goto release;                                          \
    }                                                        \
  } while (0)

// Only support set size of attr
int yfs_client::setattr(inum ino, size_t size) {
  int r = OK;

  /*
   * your code goes here.
   * note: get the content of inode ino, and modify its content
   * according to the size (<, =, or >) content length.
   */
  assert(size >= 0);
  std::string buf;

  if (ec->get(ino, buf) != extent_protocol::OK) {
    // r = IOERR;
    goto release;
  }

  buf.resize(size);

  if (ec->put(ino, buf) != extent_protocol::OK) {
    // r = IOERR;
    goto release;
  }

release:
  return r;
}

int yfs_client::create(inum parent, const char *name, mode_t mode,
                       inum &ino_out) {
  int r = OK;

  /*
   * your code goes here.
   * note: lookup is what you need to check if file exist;
   * after create file or dir, you must remember to modify the parent
   * infomation.
   */
  assert(sizeof(name) < FILENAME_SIZE);

  bool found_lookup;
  if ((lookup(parent, name, found_lookup, ino_out) != extent_protocol::OK) ||
      found_lookup) {
    r = IOERR;
    goto release;
  }

  if (ec->create(extent_protocol::T_FILE, ino_out) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  if (dir_app(parent, name, ino_out) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

release:
  return r;
}

int yfs_client::mkdir(inum parent, const char *name, mode_t mode,
                      inum &ino_out) {
  int r = OK;

  /*
   * your code goes here.
   * note: lookup is what you need to check if directory exist;
   * after create file or dir, you must remember to modify the parent
   * infomation.
   */
  assert(sizeof(name) < FILENAME_SIZE);

  bool found_lookup;
  if (lookup(parent, name, found_lookup, ino_out) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  if (found_lookup) {
    r = EXIST;
    goto release;
  }

  if (ec->create(extent_protocol::T_DIR, ino_out) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  if (dir_app(parent, name, ino_out) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

release:
  return r;
}

int yfs_client::lookup(inum parent, const char *name, bool &found,
                       inum &ino_out) {
  int r = OK;

  /*
   * your code goes here.
   * note: lookup file from parent dir according to name;
   * you should design the format of directory content.
   */
  assert(name);

  std::list<dirent> ent_list;
  if (readdir(parent, ent_list) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  found = false;
  for (std::list<dirent>::iterator it = ent_list.begin(); it != ent_list.end();
       ++it) {
    if (strcmp(it->name.c_str(), name) == 0) {
      found = true;
      ino_out = it->inum;
      break;
    }
  }

release:
  return r;
}

int yfs_client::readdir(inum dir, std::list<dirent> &list) {
  int r = OK;

  /*
   * your code goes here.
   * note: you should parse the dirctory content using your defined format,
   * and push the dirents to the list.
   */
  struct c_dirent *c_entp;
  struct dirent ent;
  std::string dir_buf;
  int cnt;

  if (ec->get(dir, dir_buf) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  assert((dir_buf.size() % sizeof(struct c_dirent)) == 0);
  cnt = dir_buf.size() / sizeof(struct c_dirent);
  c_entp = (struct c_dirent *)(dir_buf.c_str());

  while (cnt > 0) {
    ent.inum = c_entp->inum;
    ent.name.assign(c_entp->name);
    list.push_back(ent);
    cnt--;
    c_entp++;
  }

release:
  return r;
}

int yfs_client::read(inum ino, size_t size, off_t off, std::string &data) {
  int r = OK;

  /*
   * your code goes here.
   * note: read using ec->get().
   */
  assert(size >= 0);
  std::string buf;

  ec->get(ino, buf);

  if ((unsigned int)off >= buf.size()) {
    data.assign("");
    goto release;
  }

  data = buf.substr(off, size);

release:
  return r;
}

int yfs_client::write(inum ino, size_t size, off_t off, const char *data,
                      size_t &bytes_written) {
  int r = OK;

  /*
   * your code goes here.
   * note: write using ec->put().
   * when off > length of original file, fill the holes with '\0'.
   */
  std::string buf;
  std::string content;

  ec->get(ino, buf);

  content.assign(data, size);

  if ((unsigned int)off <= buf.size()) {
    buf.replace(off, size, content);
    bytes_written = size;
  } else {
    size_t old_size = buf.size();
    buf.resize(size + off, '\0');
    buf.replace(off, size, content);
    bytes_written = size + off - old_size;
  }

  ec->put(ino, buf);

  return r;
}

int yfs_client::unlink(inum parent, const char *name) {
  int r = OK;

  /*
   * your code goes here.
   * note: you should remove the file using ec->remove,
   * and update the parent directory content.
   */
  assert(name);

  std::list<dirent> ent_list;
  bool found = false;
  inum ino;
  std::string dir_buf;
  int idx;
  std::string::iterator it;
  // find entry
  if (readdir(parent, ent_list) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  idx = 0;
  for (std::list<dirent>::iterator it = ent_list.begin(); it != ent_list.end();
       ++it) {
    if (strcmp(it->name.c_str(), name) == 0) {
      found = true;
      ino = it->inum;
      break;
    }
    idx++;
  }

  if (!found) {
    r = NOENT;
    goto release;
  }

  // update directory
  if (ec->get(parent, dir_buf) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  it = dir_buf.begin() + idx * sizeof(struct c_dirent);
  dir_buf.erase(it, it + sizeof(struct c_dirent));

  if ((ec->remove(ino) != extent_protocol::OK) ||
      (ec->put(parent, dir_buf) != extent_protocol::OK)) {
    r = IOERR;
    goto release;
  }

release:
  return r;
}

int yfs_client::readlink(inum ino, std::string &data) {
  int r = OK;
  std::string buf;
  r = ec->get(ino, buf);
  data.assign(buf);

  return r;
}

int yfs_client::symlink(inum parent, const char *name, const char *link,
                        inum &ino_out) {
  int r = OK;

  assert(sizeof(name) < FILENAME_SIZE);

  struct c_dirent c_ent;
  bool found_lookup;
  if (lookup(parent, name, found_lookup, ino_out) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  if (found_lookup) {
    r = EXIST;
    goto release;
  }

  if ((ec->create(extent_protocol::T_SYMLINK, ino_out) !=
       extent_protocol::OK) ||
      ec->put(ino_out, std::string(link)) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  if (dir_app(parent, name, ino_out) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

release:
  return r;
}