<?php

use Concurrent\Timer;

var_dump('WAIT');
(new Timer(200))->awaitTimeout();
var_dump('READY');

return 'Hello World';
