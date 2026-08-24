[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$Port = 8080
$ExpectedPath = "/api/v1/chargers/EVSE-01/command"
$CommandFile = Join-Path $PSScriptRoot "command.json"
$Utf8 = New-Object System.Text.UTF8Encoding($false)

function Write-HttpJsonResponse {
    param(
        [Parameter(Mandatory = $true)] [System.Net.Sockets.NetworkStream] $Stream,
        [Parameter(Mandatory = $true)] [int] $StatusCode,
        [Parameter(Mandatory = $true)] [string] $ReasonPhrase,
        [Parameter(Mandatory = $true)] [string] $Json,
        [string] $AdditionalHeaders = ""
    )

    $Body = $Utf8.GetBytes($Json)
    $Headers = "HTTP/1.1 $StatusCode $ReasonPhrase`r`n" +
        "Content-Type: application/json; charset=utf-8`r`n" +
        "Content-Length: $($Body.Length)`r`n" +
        "Cache-Control: no-store`r`n" +
        $AdditionalHeaders +
        "Connection: close`r`n" +
        "Date: $([DateTime]::UtcNow.ToString('R'))`r`n`r`n"
    $HeaderBytes = $Utf8.GetBytes($Headers)
    $Stream.Write($HeaderBytes, 0, $HeaderBytes.Length)
    $Stream.Write($Body, 0, $Body.Length)
    $Stream.Flush()
}

function Invoke-Request {
    param([Parameter(Mandatory = $true)] [System.Net.Sockets.TcpClient] $Client)

    $Stream = $Client.GetStream()
    $Stream.ReadTimeout = 5000
    $Stream.WriteTimeout = 5000
    $Reader = New-Object System.IO.StreamReader($Stream, $Utf8, $false, 1024, $true)

    try {
        $RequestLine = $Reader.ReadLine()
        if ([string]::IsNullOrWhiteSpace($RequestLine)) {
            Write-HttpJsonResponse $Stream 400 "Bad Request" '{"error":"bad_request"}'
            return
        }

        # Consume headers. The mock endpoint does not need request bodies.
        while ($null -ne ($HeaderLine = $Reader.ReadLine()) -and $HeaderLine.Length -gt 0) { }

        $Parts = $RequestLine.Split(' ')
        if ($Parts.Length -ne 3 -or -not $Parts[2].StartsWith("HTTP/")) {
            Write-HttpJsonResponse $Stream 400 "Bad Request" '{"error":"bad_request"}'
            return
        }

        $Method = $Parts[0]
        $Path = $Parts[1].Split('?')[0]
        if ($Method -ne "GET") {
            Write-HttpJsonResponse $Stream 405 "Method Not Allowed" '{"error":"method_not_allowed","allowed":"GET"}' "Allow: GET`r`n"
            return
        }

        if ($Path -ne $ExpectedPath) {
            Write-HttpJsonResponse $Stream 404 "Not Found" ('{"error":"not_found","expected_path":"' + $ExpectedPath + '"}')
            return
        }

        try {
            $Json = [System.IO.File]::ReadAllText($CommandFile, $Utf8)
            # Parse before serving so a damaged command file never produces a false 200 response.
            $null = $Json | ConvertFrom-Json
        }
        catch {
            Write-HttpJsonResponse $Stream 500 "Internal Server Error" '{"error":"invalid_command_file"}'
            return
        }

        Write-HttpJsonResponse $Stream 200 "OK" $Json
    }
    catch [System.IO.IOException] {
        # A client can disconnect or time out before a complete request is received.
    }
    finally {
        $Reader.Dispose()
    }
}

$Listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Any, $Port)
$Listener.Start()
Write-Host "Mock API listening on http://0.0.0.0:$Port$ExpectedPath"
Write-Host "Edit $CommandFile to change the returned command. Press Ctrl+C to stop."

try {
    while ($true) {
        $Client = $Listener.AcceptTcpClient()
        try {
            Invoke-Request $Client
        }
        finally {
            $Client.Dispose()
        }
    }
}
finally {
    $Listener.Stop()
}
