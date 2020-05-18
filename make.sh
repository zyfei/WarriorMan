#!/bin/sh
/usr/local/php7/bin/phpize --clean
/usr/local/php7/bin/phpize
./configure --with-php-config=/usr/local/php7/bin/php-config
make clean
make -j4
make install