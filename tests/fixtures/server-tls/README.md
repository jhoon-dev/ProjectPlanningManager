This certificate and private key are public, disposable integration test fixtures.
They are never used by the production server launcher. Do not install them into a
machine trust store or use them for an actual deployment. Tests load the public
certificate into an isolated Python SSL context; the native client must reject it.
