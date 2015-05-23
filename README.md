ASUS Zenbook UX31A fan driver for Linux
===========
This driver provides a Linux [Thermal Framework](https://www.kernel.org/doc/Documentation/thermal/sysfs-api.txt) interface for changing and querying the fan speed on ASUS Zenbook UX31A. Moreover, the driver ensures that the fan is powered off properly on system shutdown and suspend, and restored to its previous state on system resume.

Disclaimer
===========
This software is provided by the copyright holders and contributors "as is"
and any express or implied warranties, including, but not limited to, the
implied warranties of merchantability and fitness for a particular purpose
are disclaimed. In no event shall the copyright owner or contributors be
liable for any direct, indirect, incidental, special, exemplary, or
consequential damages (including, but not limited to, procurement of
substitute goods or services; loss of use, data, or profits; or business
interruption) however caused and on any theory of liability, whether in
contract, strict liability, or tort (including negligence or otherwise)
arising in any way out of the use of this software, even if advised of the
possibility of such damage. 

Installation
===========
Build the driver from source using make, e.g.
```
$ make
$ sudo insmod ./asus_zenfan.ko
```
Optionally check whether the driver was loaded:
```
$ lsmod | grep asus_zenfan
```
A successful installation creates a directory under `/sys/class/thermal/cooling_deviceX` where X is some integer.

Background
===========
Internally, the fan driver distinguishes between two modes: *AUTO* and *MANUAL*. AUTO mode implies that the fan controller adjusts fan speed automatically. Conversely, MANUAL mode implies that the fan is controlled by a user. In MANUAL mode the fan controller will no longer adjust speed automatically. Consequently, the user must ensure that the system is properly cooled.
Furthermore, the driver also distinguishes between two states: *ACTIVE* and *SUSPENDED*. These two states are used to ensure appropriate and safe behaviour during system suspend and resume. When in ACTIVE state the driver accepts queries, but rejects queries when in SUSPENDED state.

Changing & querying fan state
============
The driver registers the fan as a Linux Thermal Framework *cooling device*. The framework exposes a generic interface for manipulating thermal devices:
```
/sys/class/thermal/cooling_deviceX/
├── cur_state    # reports/changes the current fan speed.
├── max_state    # reports the maximum settable fan speed.
└── type         # reports the cooling device type.
```
The following examples show how to change and query the fan speed, and reset the fan mode. We assume that it is installed as `cooling_device3`.

Querying fan state
-----------
We can determine the maximum settable speed:
```
cooling_device3 $ cat max_state
255
```
There is no method to determine the minimum settable speed, however as one might expect it is 0 which is equivalent to powering off the fan. Thus, the discrete range [0,255] constitute all the possible fan speed settings.

In order to read the current fan speed we query `cur_state`:
```
cooling_device3 $ cat cur_state
100
```
If the fan is in MANUAL mode it is no actually queried instead the value of the latest speed setting operation is reported. When the fan is in AUTO mode the an ACPI call is performed to report the fan speed. The reason for this difference is that querying is only supported when the fan runs in AUTO mode, for more details see [1].

To get the cooling device type simply read `type`, e.g.:
```
cooling_device3 $ cat type
Fan
```
The type `Fan` will always be returned. The type can be used by programs such as [Linux Thermal Daemon](https://01.org/linux-thermal-daemon/documentation/introduction-thermal-daemon) to determine whether the device is an active or passive cooling device.

Changing fan state
-----------
In order to change the fan speed simply write the desired speed to `cur_state`:
```
cooling_device3 $ echo 150 | tee cur_state
```
A successful write returns nothing, thus *no news is good news*!
Note that a successful write puts the fan into MANUAL mode.
You may have to perform the above write as root if your user does not have sufficient privileges.
Similary, we can power-off the fan:
```
cooling_device3 $ echo 0 | tee cur_state
```
Attempting to write a speed value less than 0 or greater than 255 results in an error:
```
cooling_device3 $ echo -25 | tee cur_state
-25
tee: cur_state: Invalid argument
cooling device3 $ echo 256 | tee cur_state
256
tee: cur_state: Invalid argument
```

Resetting the fan
-----------
By default the fan runs in AUTO mode, and any user-enforced change of fan state puts the fan into MANUAL mode. Currently, the only way to put the fan back into AUTO mode is to unload the driver, e.g.:
```
$ sudo rmmod asus_zenfan
```

TODO
============
* Provide an interface for the GPU fan on models such as Zenbook UX32VD.
* Provide an additional interface for determining the fan mode.
* Provide an additional interface for setting the fan mode to AUTO.

Acknowledgements
============
* Thanks to Felipe Contreras as this work is based on [previous work by him](https://gist.github.com/felipec/6169047). However, this driver behaves properly when the system is suspended.
* Thanks to NotebookReview forum-user "prikolchik" for providing essential knowledge about ACPI fan control primitives for the ASUS Zenbook series. The thread is available on the [NotebookReview forum][1].

References
=============
1. Forum user prikolchik. *Fan Control on Asus Prime UX31/UX31A/UX32A/UX32VD*. NotebookReview Forum. Published on January 24, 2013. [Available online][1].
[1]: http://forum.notebookreview.com/threads/fan-control-on-asus-prime-ux31-ux31a-ux32a-ux32vd.705656/
