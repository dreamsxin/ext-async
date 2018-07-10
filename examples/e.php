<?php

use Concurrent\Task;

$task = Task::async(function () {
    var_dump(321);
    
    return 777;
});

var_dump($task);

var_dump(Task::await($task));
