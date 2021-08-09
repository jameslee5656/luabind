#ifndef LUABIND_MEMORY_HPP_INCLUDED
#define LUABIND_MEMORY_HPP_INCLUDED

#include <memory>

namespace luabind {

#ifndef LUABIND_NO_STD_UNIQUE_PTR

  using std::unique_ptr;
  using std::move;

#else

  using std::auto_ptr;
  #define unique_ptr auto_ptr

  #if __cplusplus < 201103L
  template<typename T>
  T& move(T& ptr)
  {
      return ptr;
  }
  #endif // __cplusplus < 201103L

#endif // LUABIND_NO_STD_UNIQUE_PTR

} // namespace luabind

#endif // LUABIND_MEMORY_HPP_INCLUDED
