#ifndef SIGC_FIX_FUNCTOR_H
#define SIGC_FIX_FUNCTOR_H

#include <type_traits>
#include <sigc++/sigc++.h>

/* This is a fix for sigc to allow the use of lambda with return values
 * This code comes from:
 * https://mail.gnome.org/archives/libsigc-list/2012-January/msg00000.html
 */

namespace sigc
{

template <typename Functor>
struct functor_trait<Functor, false>
{
    typedef decltype (::sigc::mem_fun (std::declval<Functor&> (),
                                       &Functor::operator())) _intermediate;

    typedef typename _intermediate::result_type result_type;
    typedef Functor functor_type;
};

}

#endif // SIGC_FIX_FUNCTOR_H

