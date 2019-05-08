<?php

namespace Concurrent\Process;

use Concurrent\Network\Pipe;

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

$ipc = Process::connect();

$pipe = Pipe::import($ipc);
$ipc->close();

$pipe->write($pipe->read() . 'World!');
$pipe->close();
