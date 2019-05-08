<?php

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

throw new \RuntimeException('This is an error!');
