<?php

namespace Concurrent;

error_reporting(-1);

$context = stream_context_create([
    'http' => [
        'follow_location' => 0,
        'ignore_errors' => true
    ]
]);

var_dump(file_get_contents('http://google.com/', false, $context));
