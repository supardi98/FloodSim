import { execFile } from "child_process";
execFile("bash", ["./ya.sh"], (error, stdout, stderr) => {
  if (error) {
    console.error("Error:", error);
    return;
  }
  console.log(stdout.split("\n").slice(-2));
});
