<?php

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

var_dump(trim(fgets(STDIN)));
var_dump(trim(stream_get_contents(STDIN)));

var_dump('STDIN CLOSED');
