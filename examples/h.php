<?php

namespace Concurrent;

$timer = new Timer(100);

for ($i = 0; $i < 15; $i++) {
    $timer->awaitTimeout();
    
    var_dump($i);
}
