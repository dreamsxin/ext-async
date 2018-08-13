<?php

namespace Concurrent;

$signal = new SignalWatcher(SignalWatcher::SIGINT);

echo "START\n";

$signal->awaitSignal();

echo "END!";

exit(7);
