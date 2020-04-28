#ifndef SIMPLESTRING_H
#define SIMPLESTRING_H

#include <stddef.h>


//! @ingroup Common
struct SimpleString {
  //! The length of the string, not including the terminating nul byte.
  size_t len;
  //! The number of bytes that can be stored in the string.
  size_t allocated_len;
  //! The character data
  char str[0];
};


#define SIMPLE_STRING(x) \
  ((struct SimpleString *) ((char *) (x) - offsetof(struct SimpleString, str)))
#define SIMPLE_STRING_SIZE(x) ((x) + offsetof(struct SimpleString, str))


#endif /* SIMPLESTRING_H */
