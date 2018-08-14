<?php

var_dump(getenv('PATH') ? 'Y' : 'N');
usleep(10000);
var_dump(getenv('FOO'));
