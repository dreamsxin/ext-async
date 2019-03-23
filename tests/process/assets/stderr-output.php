<?php

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

fwrite(STDERR, 'Hello');
usleep(10000);

fwrite(STDERR, 'World :)');

exit(1);
