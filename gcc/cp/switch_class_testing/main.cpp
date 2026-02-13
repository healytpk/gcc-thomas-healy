#include <iostream>
using std::cout, std::endl;

struct S {
  template<typename T>
  bool operator==(T &&arg) const && noexcept  { if ( !static_cast<bool>(arg) ) return true; }
};

int main(void)
{
  S const s;
  
  switch class ( s )
  {
  default:
    cout << "default\n";
  case "monkey":
    cout << "monkey\n";
  case "frog":
    cout << "frog\n";
  case "chocolate":
     ;
  }
  
}
