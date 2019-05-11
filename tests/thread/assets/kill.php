<?php

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

var_dump('RUN');

for ($i = 0; $i < 100; $i++) {
    usleep(10000);
}

var_dump('FINISHED');
