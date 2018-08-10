<?php

namespace Concurrent;

$builder = new ProcessBuilder('ls');
$builder->configureStdout(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDOUT);
$builder->configureStderr(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDOUT);

$proccess = $builder->start(...\array_slice($_SERVER['argv'], 1));

var_dump($proccess);

$code = $proccess->awaitExit();

var_dump($proccess);

// $code = $builder->execute(...\array_slice($_SERVER['argv'], 1));

echo "\nEXIT CODE: ", $code, "\n";
