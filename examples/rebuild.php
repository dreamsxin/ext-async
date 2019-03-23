<?php

namespace Concurrent;

var_dump('START');

(new Timer(10))->awaitTimeout();

var_dump('DONE MAIN');
