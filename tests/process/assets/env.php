<?php

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

echo getenv('PATH');
usleep(10000);
echo getenv('FOO');
