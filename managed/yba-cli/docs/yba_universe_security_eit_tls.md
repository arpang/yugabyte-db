## yba universe security eit tls

Toggle TLS settings for a universe

### Synopsis

Toggle TLS settings for a universe

```
yba universe security eit tls [flags]
```

### Examples

```
yba universe security eit tls --name <universe-name> \
	--client-to-node-encryption <client-to-node-encryption>
```

### Options

```
      --client-to-node-encryption string   [Optional] Client to node encryption. Allowed values: enable, disable.
      --node-to-node-encryption string     [Optional] Node to node encryption. Allowed values: enable, disable.
      --root-and-client-root-ca-same       [Optional] Use same certificates for node to node and client to node communication. (default true)
  -h, --help                               help for tls
```

### Options inherited from parent commands

```
  -a, --apiToken string         YugabyteDB Anywhere api token.
      --ca-cert string          CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string           Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug                   Use debug mode, same as --logLevel debug.
      --directory string        Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color           Disable colors in output. (default false)
  -f, --force                   [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string             YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure                Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string         Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string             [Required] The name of the universe for the operation.
  -o, --output string           Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations        [Optional] Skip validations before running the CLI command.
      --timeout duration        Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --upgrade-option string   [Optional] Upgrade Options, defaults to Rolling. Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime). Only a "Non-Rolling" type of restart is allowed for TLS upgrade. (default "Rolling")
      --wait                    Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe security eit](yba_universe_security_eit.md)	 - Encryption-in-transit settings for a universe

