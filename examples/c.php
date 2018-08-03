<?php

namespace Concurrent;

$host = 'www.google.com';

var_dump($host, gethostbyname($host));

$var = new ContextVar();

var_dump($var->get());

$context = Context::current();
$context = $context->with($var, 321);

$context->run(function () use ($var) {
    var_dump($var->get());
});

var_dump($var->get());
var_dump($var->get($context));
var_dump($var->get());
