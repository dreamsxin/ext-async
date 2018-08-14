<?php

var_dump('AWAIT SIGNAL');

pcntl_signal(2, function () {
    var_dump('SIGNAL RECEIVED');
    
    exit(4);
});

pcntl_async_signals(true);

sleep(1);
