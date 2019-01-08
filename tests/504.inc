<?php

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

var_dump('AWAIT SIGNAL');

pcntl_signal(2, function () {
    var_dump('SIGNAL RECEIVED');
    
    exit(4);
});

pcntl_async_signals(true);

sleep(1);
