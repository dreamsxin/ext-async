<?php

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

var_dump(getenv('PATH') ? 'Y' : 'N');
usleep(10000);
var_dump(getenv('FOO'));
