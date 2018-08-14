<?php

namespace Concurrent;

var_dump('AWAIT SIGNAL');

(new SignalWatcher(SignalWatcher::SIGINT))->awaitSignal();

var_dump('SIGNAL RECEIVED');

exit(4);
