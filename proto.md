LCLS2 ACM Ethernet Protocol
===========================

A one-way streaming UDP protocol between a Accumulated Charge Monitor device
(Sender) and a Receiver.

Sending devices (ACMs) may use more than one UDP port.
All packets sent from a single device (IP Address) are considered part of a single data stream.
Each Receiver must be configured with an IP and a list of port numbers to listen on.

This stream consists of sequences of packets which are grouped together.

UDP Message Format
------------------

All messages begin with a common 8 byte header.

<pre>
           0       1         2         3
      +--------+--------+--------+--------+
   00 |   ID   | Flags  |    Sequence#    |
      +--------+--------+--------+--------+
   04 |           Timebase Counter        |
      +--------+--------+--------+--------+
      ... body ...
</pre>

Depending on the value of the ID field one of two body formats is used.

* ID=0x51 - Register Data
* ID=0xe7 - Sample data from fault trip
* ID=0x33 - Sample data from internal trigger
* ID=0x28 - Sample data from external trigger

Packets with other ID numbers are considered invalid and must be ignored.

The flags field include several sub-fields

* flags[0] - Set if a message is the final/last message in a sequence.
* flags[7:1] - Reserved.  Sender zeros, Receiver ignores.

Each UDP packet will contain only a single protocol message,
so the message body is the remainder of the packet after
the 8 byte ACM header.

Register Data Body
------------------

The body of packets with ID==0x51 are interpreted as an array of
32-bit register values.  The precise interpretation of these
values is left to application logic and not defined here.

A packet is not considered valid unless it contains at least
one register value (body length 4).

A packet with a body length which is not a multiple of 4 bytes
is considered invalid.

<pre>
           0       1         2         3
      +--------+--------+--------+--------+
   08 |          Register Value 0         |
      +--------+--------+--------+--------+
   0c |          Register Value 1         |
      +--------+--------+--------+--------+
      ... additional values
</pre>


Sample Data Body
----------------

The body of packets with IDs of 0xe7, 0x33, and 0x28
are interpreted as a list of tuples at different time points.

A packet is not considered valid unless it contains at least
one tuple (body length 16).

A packet with a body length which is not a multiple of 16 bytes
is considered invalid.

<pre>
           0       1         2         3
      +--------+--------+--------+--------+
   08 |      I[0]       |      Q[0]       |
      +--------+--------+--------+--------+
   0c |  0x00  |        Current[0]        |
      +--------+--------+--------+--------+
   10 |  0x00  |        Phase[0]          |
      +--------+--------+--------+--------+
   14 |           Integrator[0]           |
      +--------+--------+--------+--------+
   18 |      I[1]       |      Q[1]       |
      +--------+--------+--------+--------+
   1c |  0x00  |        Current[1]        |
      +--------+--------+--------+--------+
   20 |  0x00  |        Phase[1]          |
      +--------+--------+--------+--------+
   24 |           Integrator[1]           |
      +--------+--------+--------+--------+
      ... Repeated ...
</pre>

Sequence Handling
-----------------

All packets are subject to sequence reconstruction.

A sequence is identified by a tuple of: Source IP address, ID field value, and Timebase count.
Packets within a sequence are identified by a Sequence number beginning with zero,
with the final (or only) packet in a sequence having the flags[0] bit set.

In the trivial case where a sequence contains only one packet,
this packet must have sequence number 0, and flags[0]=1.

A sequence is considered completely received after packets with
Sequence numbers 0 through N are received, where the packet with
Sequence number N has flags[0]=1.
Any packets with Sequence numbers greater than N are ignored.

A Receiver may expect to receive all packets in a sequence
within a short time of receiving any packet in that sequence.


Timebase Counter
----------------

The Timebase counter field present in all packets is interpreted as
as device internal realtime clock with an undefined epoch (aka. zero
is device power up/reset).

This counter is incremented at a predetermined rate of XXXMHz (TBD)
and will roll over on overflow.

Each sequence is assigned a Timebase counter value.
The relationship of this count value to the data sequence data
depends on message body type.

### Register Data

A Register data sequence is interpreted as a snapshot of values at
the time represented by the count value.

### Sample Data

A Sample data sequence is interpreted as a time series of tuples, with the count
value representing the time of a particular (reference) sample index within the series.

The index of the reference sample depends on the ID field.

* ID=0xe7 - Index XXX (TBD)
* ID=0x33 - Index XXX (TBD)
* ID=0x28 - Index XXX (TBD)

The sample rate of time points within a series is the Timebase counter rate.
