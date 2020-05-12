/* workerman extension for PHP */

#ifndef PHP_WORKERMAN_H
# define PHP_WORKERMAN_H

#include "header.h"

# define PHP_WORKERMAN_VERSION "0.1.0"

extern zend_module_entry workerman_module_entry;
# define phpext_workerman_ptr &workerman_module_entry

#ifdef PHP_WIN32
#    define PHP_WORKERMAN_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#    define PHP_WORKERMAN_API __attribute__ ((visibility("default")))
#else
#    define PHP_WORKERMAN_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif


/**
 * Declare any global variables you may need between the BEGIN and END macros here
 */
ZEND_BEGIN_MODULE_GLOBALS(workerman)

ZEND_END_MODULE_GLOBALS(workerman)

#endif	/* PHP_WORKERMAN_H */

