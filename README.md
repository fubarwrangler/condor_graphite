# HTCondor CGroup Reader

Program for reading stats about running HTCondor jobs on a machine from their
CGroups and publishing this to Graphite.

## Prerequisites
HTCondor must be configured to use CGroup-tracking of jobs. See
[this part](http://research.cs.wisc.edu/htcondor/manual/v8.4/3_12Setting_Up.html#sec:CGroupTracking)
of the condor manual for information on how to set this up. Note that this does
*not* mean that CGroup limits must be enforced, just that condor classifies
its jobs into cgroups so statistics can be gathered.

## Usage
```
condor_cg_graphite [-p PATH] [-c CGROUP] GRAPHITE_HOST

GRAPHITE_HOST is either <hostname>:<port> or just <hostname> (with the port
defaulting to the standard line-protocol port 2003)

Options:
	-c CGROUP: condor cgroup name (default htcondor)
	-p PATH: metric path prefix in graphite (default htcondor.cgroups)
	-h show this usage help
```

## Issues and Limitations
This software sends plaintext UDP or TCP packets to graphite, not
pickle-protocol so graphite must be configured accordingly.

Assumes that the condor-cgroup name is derived in a consistent manner, I'm not
sure if this is a stable interface in the HTCondor source code (it may change
one day?)

## Installation
```
$ cmake .
$ make
```

or

```
$ cmake .
$ make package
```

to get an RPM

## Ideas
We may want to gather information about the job-owner and other attributes
from each cgroup (how?)

Can we date each cgroup's creation time?
