#ifndef ACTIVITYINTL_H
#define ACTIVITYINTL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_GETTEXT
# define _(x) gettext(x)
#else
# define _(x) (x)
#endif

#endif /* ACTIVITYINTL_H */
