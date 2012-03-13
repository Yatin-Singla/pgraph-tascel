# PT_PROG_MPICXX
# --------------
# If desired, replace CXX with MPICXX while searching for a C++ compiler.
#
# Known C++ compilers:
#  aCC      HP-UX C++ compiler much better than `CC', so test before.
#  c++
#  cc++
#  CC
#  cl.exe
#  cxx
#  FCC      Fujitsu C++ compiler
#  g++
#  gpp
#  icpc     Intel C++ compiler
#  KCC      KAI C++ compiler
#  RCC      Rational C++
#  bgxlC    Intel
#  bgxlC_r  Intel, thread safe
#  xlC      AIX C Set++
#  xlC_r    AIX C Set++, thread safe
#  pgCC     Portland Group
#  pathCC   PathScale
#  sxc++    NEC SX
#  openCC   AMD's x86 open64
#  sunCC    Sun's Studio
#  crayc++  Cray
#
# Known MPI C++ compilers
#  mpic++
#  mpicxx
#  mpiCC
#  sxmpic++     NEC SX
#  hcp
#  mpxlC_r
#  mpxlC
#  mpixlcxx_r
#  mpixlcxx
#  mpg++
#  mpc++
#  mpCC
#  cmpic++
#  mpiFCC       Fujitsu
#  CC
#
AC_DEFUN([PT_PROG_MPICXX],
[AC_ARG_VAR([MPICXX], [MPI C++ compiler])
# In the case of using MPI wrappers, set CXX=MPICXX since CXX will override
# absolutely everything in our list of compilers.
AS_IF([test x$with_mpi_wrappers = xyes],
    [AS_IF([test "x$CXX" != "x$MPICXX"], [pt_orig_CXX="$CXX"])
     AS_CASE([x$CXX:x$MPICXX],
        [x:x],  [],
        [x:x*], [CXX="$MPICXX"],
        [x*:x],
[AC_MSG_WARN([MPI compilers desired but CXX is set while MPICXX is unset.])
 AC_MSG_WARN([CXX will be ignored during compiler selection, but will be])
 AC_MSG_WARN([tested first during MPI compiler unwrapping. Perhaps you])
 AC_MSG_WARN([meant to set MPICXX instead of or in addition to CXX?])
 CXX=],
        [x*:x*], 
[AS_IF([test "x$CXX" != "x$MPICXX"],
[AC_MSG_WARN([MPI compilers desired, MPICXX and CXX are set, and MPICXX!=CXX.])
 AC_MSG_WARN([Choosing MPICXX over CXX.])
 AC_MSG_WARN([CXX will be tested first during MPI compiler unwrapping.])])
 CXX="$MPICXX"],
[AC_MSG_ERROR([CXX/MPICXX case failure])])])
pt_cxx="icpc pgCC pathCC sxc++ xlC_r xlC bgxlC_r bgxlC openCC sunCC crayc++ g++ c++ gpp aCC CC cxx cc++ cl.exe FCC KCC RCC"
pt_mpicxx="mpic++ mpicxx mpiCC sxmpic++ hcp mpxlC_r mpxlC mpixlcxx_r mpixlcxx mpg++ mpc++ mpCC cmpic++ mpiFCC CC"
AS_IF([test x$with_mpi_wrappers = xyes],
    [CXX_TO_TEST="$pt_mpicxx"],
    [CXX_TO_TEST="$pt_cxx"])
AC_PROG_CXX([$CXX_TO_TEST])
])dnl
