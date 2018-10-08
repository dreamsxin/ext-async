<?php

namespace Concurrent;

var_dump(SignalWatcher::isSupported(SignalWatcher::SIGINT));
var_dump(SignalWatcher::isSupported(SignalWatcher::SIGUSR1));

$watcher = new SignalWatcher(SignalWatcher::SIGINT);

echo "Listening for SIGINT, press CTRL + C to terminate...\n";

$watcher->awaitSignal();

echo "\n=> Signal received :)\n";
