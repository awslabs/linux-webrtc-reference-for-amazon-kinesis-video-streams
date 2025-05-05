# CI Setup

## GitHub Action to generate IoT certificates using AWS

```
1 Create an OIDC provider in AWS IAM:
aws iam create-open-id-connect-provider \
  --url https://token.actions.githubusercontent.com \
  --thumbprint-list "6938fd4d98bab03faadb97b34396831e3780aea1" \
  --client-id-list "sts.amazonaws.com"


2a Manually: Create an IAM Role with this trust relationship (Custom trust policy):
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Principal": {
                "Federated": "arn:aws:iam::<YOUR_AWS_ACCOUNT_ID>:oidc-provider/token.actions.githubusercontent.com"
            },
            "Action": "sts:AssumeRoleWithWebIdentity",
            "Condition": {
                "StringLike": {
                    "token.actions.githubusercontent.com:sub": "repo:<YOUR_GITHUB_ORG>/<YOUR_REPO>:*"
                }
            }
        }
    ]
}
3 Using cli:
First create a trust policy file named trust-policy.json:
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Principal": {
                "Federated": "arn:aws:iam::YOUR_AWS_ACCOUNT_ID:oidc-provider/token.actions.githubusercontent.com"
            },
            "Action": "sts:AssumeRoleWithWebIdentity",
            "Condition": {
                "StringLike": {
                    "token.actions.githubusercontent.com:sub": "repo:YOUR_GITHUB_ORG/YOUR_REPO:*"
                }
            }
        }
    ]
}

Create a permissions policy file named permissions-policy.json:

{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": [
                "iot:CreateKeysAndCertificate",
                "iot:CreateThing",
                "iot:CreatePolicy",
                "iot:AttachPolicy",
                "iot:AttachThingPrincipal",
                "iot:CreateRoleAlias",
                "iot:DescribeEndpoint",
                "iot:DeleteThing",
                "iot:DeleteCertificate",
                "iot:DeletePolicy",
                "iot:DeleteRoleAlias",
                "iot:DetachPolicy",
                "iot:DetachThingPrincipal",
                "iot:ListAttachedPolicies",
                "iot:ListThingPrincipals",
                "iot:UpdateCertificate"
            ],
            "Resource": "*"
        },
        {
            "Effect": "Allow",
            "Action": "iam:PassRole",
            "Resource": "arn:aws:iam::743600277648:role/*",
            "Condition": {
                "StringEquals": {
                    "iam:PassedToService": "iot.amazonaws.com"
                }
            }
        }
    ]
}
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": [
                "iam:CreateRole",
                "iam:GetRole",
                "iam:PutRolePolicy",
                "iam:DeleteRole",
                "iam:DeleteRolePolicy",
                "iam:ListRolePolicies",
                "iam:ListAttachedRolePolicies"
            ],
            "Resource": "arn:aws:iam::743600277648:role/KVSWebRTCRole_*"
        }
    ]
}
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": [
                "kinesisvideo:CreateSignalingChannel",
                "kinesisvideo:DeleteSignalingChannel",
                "kinesisvideo:DescribeSignalingChannel"
            ],
            "Resource": "arn:aws:kinesisvideo:*:*:channel/*"
        }
    ]
}

# Create the IAM role with trust policy
aws iam create-role \
    --role-name github-actions-iot-role \
    --path "/" \
    --assume-role-policy-document file://trust-policy.json

# Attach the permissions policy
aws iam put-role-policy \
  --role-name github-actions-iot-role \
  --policy-name iot-cert-permissions \
  --policy-document file://permissions-policy.json



```

The role ARN will be: arn:aws:iam::YOUR_AWS_ACCOUNT_ID:role/github-actions-iot-role

This is what you'll use in your GitHub Actions workflow in the role-to-assume field.

## setup github runner on CodeBuild

```
git clone https://github.com/aws4embeddedlinux/aws4embeddedlinux-ci-examples.git
cd aws4embeddedlinux-ci-examples
npm install .
npm run build
```

```
cdk bootstrap
cdk deploy EmbeddedLinuxCodeBuildProject --require-approval=never
```

## Login into CodeBuild
- clone CodeBuild project - meta-aws-demos, meta-aws

- Manage default source credential -> OAuth app -> CodeBuild managed token -> connect to GitHub -> confirm

> [!IMPORTANT]
> If you are not selecting OAuth app, you will not see aws4embeddedlinux repos!

```
Select "Primary source webhook events" -> "Webhook - optional" -> "Rebuild every time a code change is pushed to this repository"

Add "Filter group 1" -> "WORKFLOW_JOB_QUEUED"

In the GitHub workflow:
Modify the GitHub action runs-on: ${{ vars.CODEBUILD_RUNNER_NAME }}-${{ github.run_id }}-${{ github.run_attempt }} CODEBUILD_RUNNER_NAME should be codebuild-EmbeddedLinuxCodebuildProjeNAME with prefix codebuild
```