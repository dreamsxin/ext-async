--TEST--
Task schedulers can be stacked and unstacked.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::register($s1 = new TaskScheduler());

Task::await(Task::async('var_dump', 'A'));

TaskScheduler::register($s2 = new TaskScheduler());

$t = Task::async('var_dump', 'B');
Task::async('var_dump', 'C');

Task::await($t);

TaskScheduler::unregister($s2);

Task::await(Task::async('var_dump', 'D'));
Task::async('var_dump', 'E');

TaskScheduler::unregister($s1);

Task::await(Task::async('var_dump', 'X'));

--EXPECT--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
string(1) "E"
string(1) "X"
