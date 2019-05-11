<?php

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

var_dump('GO THROW');

throw new \RuntimeException('This is an error!');
