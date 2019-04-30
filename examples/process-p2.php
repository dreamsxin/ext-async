<?php

namespace Concurrent;

$signal = new Signal(Signal::SIGINT);
var_dump(getenv('PATH'));
echo "START: \"", getenv('MY_TITLE'), "\"\n";

$signal->awaitSignal();

echo "END!";

exit(7);
