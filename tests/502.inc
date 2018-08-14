<?php

fwrite(STDERR, 'Hello');
usleep(10000);

fwrite(STDERR, 'World :)');

exit(1);
