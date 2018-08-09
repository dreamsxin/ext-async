<?php

namespace Concurrent;

$builder = new ProcessBuilder('ls');
$builder->configureStdout(ProcessBuilder::STDIO_INHERIT, 1);
$builder->configureStderr(ProcessBuilder::STDIO_INHERIT, 1);

$code = $builder->execute(...\array_slice($_SERVER['argv'], 1));

echo "\nEXIT CODE: ", $code, "\n";
