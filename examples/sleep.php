<?php

namespace Concurrent;

Task::asyncWithContext(Context::current()->background(), function () {
    for ($i = 0; $i < 20; $i++) {
        sleep(1);
        var_dump('B');
    }
});

var_dump('START');

for ($i = 0; $i < 4; $i++) {
    sleep(2);
    var_dump('A');
}

var_dump('END');
