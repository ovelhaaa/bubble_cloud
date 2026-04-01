import urllib.request
with urllib.request.urlopen('https://developer.mozilla.org/en-US/docs/Web/API/Worklet/addModule') as response:
    html = response.read()
    if b"options" in html:
        print("options exist")
