<?php

var_dump(trim(fgets(STDIN)));
var_dump(stream_get_contents(STDIN));

var_dump('STDIN CLOSED');
