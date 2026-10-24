# Security policy

## Supported versions

Security fixes are provided for the latest published release and the `main`
branch. Older firmware versions may not receive backports.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Use
[GitHub private vulnerability reporting](https://github.com/Medzoner/domoticz-esp32cam/security/advisories/new)
and include:

- the affected release or commit;
- the affected board and camera configuration;
- reproduction steps or a proof of concept;
- the impact and expected attack conditions; and
- any suggested mitigation.

You should receive an acknowledgement within seven days. The maintainer will
coordinate validation, remediation, and disclosure through the advisory. Please
allow a reasonable remediation period before publishing details.

## Deployment boundary

This firmware serves video and credentials over plain HTTP. Basic
authentication does not provide encryption. Deploy cameras only on an isolated,
trusted IoT network behind appropriate firewall rules. Do not expose a device
directly to the Internet. See the README security section for residual risks.
