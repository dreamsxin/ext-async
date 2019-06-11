<?php

require __DIR__ . '/../skipif.inc';

if (!Concurrent\Thread::isAvailable()) {
    die('skip');
}
