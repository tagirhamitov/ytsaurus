package integration

import (
	"context"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	"a.yandex-team.ru/library/go/ptr"
	"a.yandex-team.ru/yt/go/guid"
	"a.yandex-team.ru/yt/go/ypath"
	"a.yandex-team.ru/yt/go/yt"
)

func TestAdminClient(t *testing.T) {
	suite := NewSuite(t)

	RunClientTests(t, []ClientTest{
		{Name: "AddRemoveMember", Test: suite.TestAddRemoveMember},
		{Name: "TransferAccountResources", Test: suite.TestTransferAccountResources},
		{Name: "TransferPoolResources", Test: suite.TestTransferPoolResources, SkipRPC: true},
		{Name: "CheckPermission", Test: suite.TestCheckPermission},
		{Name: "CheckColumnPermission", Test: suite.TestCheckColumnPermission},
	})
}

func (s *Suite) TestAddRemoveMember(t *testing.T, yc yt.Client) {
	t.Parallel()

	group := "group-" + guid.New().String()
	_ = s.CreateGroup(t, group)

	user := "user-" + guid.New().String()
	_ = s.CreateUser(t, user)

	require.False(t, s.MemberOf(t, user, group))

	err := yc.AddMember(s.Ctx, group, user, nil)
	require.NoError(t, err)
	require.True(t, s.MemberOf(t, user, group))

	err = yc.RemoveMember(s.Ctx, group, user, nil)
	require.NoError(t, err)
	require.False(t, s.MemberOf(t, user, group))
}

func (s *Suite) TestTransferAccountResources(t *testing.T, yc yt.Client) {
	t.Parallel()

	mebibyte := 1024 * 1024
	from := "account-from-" + guid.New().String()
	_ = s.CreateAccount(t, from, map[string]interface{}{
		"resource_limits": map[string]interface{}{
			"disk_space_per_medium": map[string]interface{}{
				"default": 10 * mebibyte,
			},
			"node_count":  20,
			"chunk_count": 30,
		},
	})

	to := "account-to-" + guid.New().String()
	_ = s.CreateAccount(t, to, map[string]interface{}{
		"resource_limits": map[string]interface{}{
			"disk_space_per_medium": map[string]interface{}{
				"default": 5 * mebibyte,
			},
			"node_count":  10,
			"chunk_count": 15,
		},
	})

	err := yc.TransferAccountResources(s.Ctx, from, to, map[string]interface{}{
		"disk_space_per_medium": map[string]interface{}{
			"default": 3 * mebibyte,
		},
		"node_count":  6,
		"chunk_count": 9,
	}, nil)

	require.NoError(t, err)

	require.Equal(t, 7*mebibyte, s.getAccountResourceAttribute(t, yc, from, "disk_space_per_medium/default"))
	require.Equal(t, 14, s.getAccountResourceAttribute(t, yc, from, "node_count"))
	require.Equal(t, 21, s.getAccountResourceAttribute(t, yc, from, "chunk_count"))

	require.Equal(t, 8*mebibyte, s.getAccountResourceAttribute(t, yc, to, "disk_space_per_medium/default"))
	require.Equal(t, 16, s.getAccountResourceAttribute(t, yc, to, "node_count"))
	require.Equal(t, 24, s.getAccountResourceAttribute(t, yc, to, "chunk_count"))
}

func (s *Suite) TestTransferPoolResources(t *testing.T, yc yt.Client) {
	t.Parallel()

	poolTree := "tree-" + guid.New().String()
	_ = s.CreatePoolTree(t, poolTree)

	from := "pool-from-" + guid.New().String()
	_ = s.CreatePool(t, from, poolTree, map[string]interface{}{
		"strong_guarantee_resources": map[string]interface{}{"cpu": 10},
		"integral_guarantees": map[string]interface{}{
			"resource_flow":             map[string]interface{}{"cpu": 20},
			"burst_guarantee_resources": map[string]interface{}{"cpu": 30},
		},
		"max_running_operation_count": 40,
		"max_operation_count":         50,
	})

	to := "pool-to-" + guid.New().String()
	_ = s.CreatePool(t, to, poolTree, map[string]interface{}{
		"strong_guarantee_resources": map[string]interface{}{"cpu": 10},
		"integral_guarantees": map[string]interface{}{
			"resource_flow":             map[string]interface{}{"cpu": 20},
			"burst_guarantee_resources": map[string]interface{}{"cpu": 30},
		},
		"max_running_operation_count": 40,
		"max_operation_count":         50,
	})

	err := yc.TransferPoolResources(s.Ctx, from, to, poolTree, map[string]interface{}{
		"strong_guarantee_resources":  map[string]interface{}{"cpu": 4},
		"resource_flow":               map[string]interface{}{"cpu": 8},
		"burst_guarantee_resources":   map[string]interface{}{"cpu": 12},
		"max_running_operation_count": 16,
		"max_operation_count":         20,
	}, nil)

	require.NoError(t, err)
	require.Equal(t, 6.0, s.getPoolAttribute(t, yc, poolTree, from, "strong_guarantee_resources/cpu"))
	require.Equal(t, 12.0, s.getPoolAttribute(t, yc, poolTree, from, "integral_guarantees/resource_flow/cpu"))
	require.Equal(t, 18.0, s.getPoolAttribute(t, yc, poolTree, from, "integral_guarantees/burst_guarantee_resources/cpu"))
	require.Equal(t, 24.0, s.getPoolAttribute(t, yc, poolTree, from, "max_running_operation_count"))
	require.Equal(t, 30.0, s.getPoolAttribute(t, yc, poolTree, from, "max_operation_count"))

	require.Equal(t, 14.0, s.getPoolAttribute(t, yc, poolTree, to, "strong_guarantee_resources/cpu"))
	require.Equal(t, 28.0, s.getPoolAttribute(t, yc, poolTree, to, "integral_guarantees/resource_flow/cpu"))
	require.Equal(t, 42.0, s.getPoolAttribute(t, yc, poolTree, to, "integral_guarantees/burst_guarantee_resources/cpu"))
	require.Equal(t, 56.0, s.getPoolAttribute(t, yc, poolTree, to, "max_running_operation_count"))
	require.Equal(t, 70.0, s.getPoolAttribute(t, yc, poolTree, to, "max_operation_count"))
}

func (s *Suite) getPoolAttribute(t *testing.T, yc yt.Client, poolTree string, pool string, attribute string) float64 {
	t.Helper()
	var result float64
	path := ypath.Path("//sys/pool_trees").Child(poolTree).Child(pool).Attr(attribute)
	require.NoError(t, yc.GetNode(s.Ctx, path, &result, nil))
	return result
}

func (s *Suite) getAccountResourceAttribute(t *testing.T, yc yt.Client, account string, attribute string) int {
	t.Helper()
	var result int
	path := ypath.Path("//sys/accounts").Child(account).Attr("resource_limits").Child(attribute)
	require.NoError(t, yc.GetNode(s.Ctx, path, &result, nil))
	return result
}

func (s *Suite) CreateGroup(t *testing.T, name string) yt.NodeID {
	t.Helper()

	id, err := s.YT.CreateObject(s.Ctx, yt.NodeGroup, &yt.CreateObjectOptions{
		Attributes: map[string]interface{}{
			"name": name,
		}})

	require.NoError(t, err)
	return id
}

func (s *Suite) CreateUser(t *testing.T, user string) yt.NodeID {
	t.Helper()

	id, err := s.YT.CreateObject(s.Ctx, yt.NodeUser, &yt.CreateObjectOptions{
		Attributes: map[string]interface{}{
			"name": user,
		},
	})

	require.NoError(t, err)
	return id
}

func (s *Suite) MemberOf(t *testing.T, user, group string) bool {
	t.Helper()

	var groups []string
	err := s.YT.GetNode(s.Ctx, ypath.Path("//sys/users").Child(user).Attr("member_of"), &groups, nil)
	require.NoError(t, err)

	for _, g := range groups {
		if g == group {
			return true
		}
	}

	return false
}

func (s *Suite) CreateAccount(t *testing.T, accountName string, attributes map[string]interface{}) yt.NodeID {
	t.Helper()

	attributes["name"] = accountName
	id, err := s.YT.CreateObject(s.Ctx, yt.NodeAccount, &yt.CreateObjectOptions{
		Attributes: attributes,
	})

	require.NoError(t, err)
	return id
}

func (s *Suite) CreatePool(t *testing.T, poolName string, poolTree string, attributes map[string]interface{}) yt.NodeID {
	t.Helper()

	attributes["name"] = poolName
	attributes["pool_tree"] = poolTree
	id, err := s.YT.CreateObject(s.Ctx, yt.NodeSchedulerPool, &yt.CreateObjectOptions{
		Attributes: attributes,
	})

	require.NoError(t, err)
	return id
}

func (s *Suite) CreatePoolTree(t *testing.T, poolTreeName string) yt.NodeID {
	t.Helper()

	id, err := s.YT.CreateObject(s.Ctx, yt.NodeSchedulerPoolTree, &yt.CreateObjectOptions{
		Attributes: map[string]interface{}{
			"name": poolTreeName,
		},
	})

	require.NoError(t, err)
	return id
}

func (s *Suite) TestCheckPermission(t *testing.T, yc yt.Client) {
	t.Parallel()

	ctx, cancel := context.WithTimeout(s.Ctx, time.Second*30)
	defer cancel()

	user := guid.New().String()
	userID, err := yc.CreateObject(ctx, yt.NodeUser, &yt.CreateObjectOptions{
		Attributes: map[string]interface{}{
			"name": user,
		},
	})
	require.NoError(t, err)

	permissions := []yt.Permission{
		yt.PermissionRead,
		yt.PermissionWrite,
		yt.PermissionUse,
		yt.PermissionAdminister,
		yt.PermissionCreate,
		yt.PermissionRemove,
		yt.PermissionMount,
		yt.PermissionManage,
		yt.PermissionModifyChildren,
	}

	for _, nodePermission := range permissions {
		p := tmpPath()
		nodeID, err := yc.CreateNode(ctx, p, yt.NodeMap, &yt.CreateNodeOptions{
			Attributes: map[string]interface{}{
				"acl": []yt.ACE{
					{
						Action:      yt.ActionAllow,
						Subjects:    []string{user},
						Permissions: []yt.Permission{nodePermission},
					},
				},
				"inherit_acl": false,
			},
		})
		require.NoError(t, err)

		for _, userPermission := range permissions {
			response, err := yc.CheckPermission(ctx, user, userPermission, p, nil)
			require.NoError(t, err)

			if userPermission == nodePermission {
				expectedResponse := &yt.CheckPermissionResponse{
					CheckPermissionResult: yt.CheckPermissionResult{
						Action:      yt.ActionAllow,
						ObjectID:    nodeID,
						ObjectName:  ptr.String("node " + p.YPath().String()),
						SubjectID:   userID,
						SubjectName: &user,
					},
				}
				require.Equal(t, expectedResponse, response)
			} else {
				expectedResponse := &yt.CheckPermissionResponse{
					CheckPermissionResult: yt.CheckPermissionResult{
						Action: yt.ActionDeny,
					},
				}
				require.Equal(t, expectedResponse, response)
			}
		}
	}
}

func (s *Suite) TestCheckColumnPermission(t *testing.T, yc yt.Client) {
	t.Parallel()

	ctx, cancel := context.WithTimeout(s.Ctx, time.Second*30)
	defer cancel()

	user1 := guid.New().String()
	user1ID, err := yc.CreateObject(ctx, yt.NodeUser, &yt.CreateObjectOptions{
		Attributes: map[string]interface{}{
			"name": user1,
		},
	})

	require.NoError(t, err)
	user2 := guid.New().String()
	_, err = yc.CreateObject(ctx, yt.NodeUser, &yt.CreateObjectOptions{
		Attributes: map[string]interface{}{
			"name": user2,
		},
	})
	require.NoError(t, err)

	var usersGroupID yt.NodeID
	err = yc.GetNode(ctx, ypath.Path("//sys/groups/users/@id"), &usersGroupID, nil)
	require.NoError(t, err)

	p := tmpPath()
	nodeID, err := yc.CreateNode(ctx, p, yt.NodeMap, &yt.CreateNodeOptions{
		Attributes: map[string]interface{}{
			"acl": []yt.ACE{
				{
					Action:      yt.ActionAllow,
					Subjects:    []string{"users"},
					Permissions: []yt.Permission{yt.PermissionRead},
				},
				{
					Action:      yt.ActionAllow,
					Subjects:    []string{user1},
					Permissions: []yt.Permission{yt.PermissionRead},
					Columns:     []string{"protected_column"},
				},
			},
			"inherit_acl": false,
		},
	})
	require.NoError(t, err)

	allowUsersResult := yt.CheckPermissionResult{
		Action:      yt.ActionAllow,
		ObjectID:    nodeID,
		ObjectName:  ptr.String("node " + p.YPath().String()),
		SubjectID:   usersGroupID,
		SubjectName: ptr.String("users"),
	}

	// check user1's permissions

	response, err := yc.CheckPermission(ctx, user1, yt.PermissionRead, p, &yt.CheckPermissionOptions{
		Columns: []string{"protected_column", "regular_column"},
	})
	require.NoError(t, err)

	expectedResponse := &yt.CheckPermissionResponse{
		CheckPermissionResult: allowUsersResult,
		Columns: []yt.CheckPermissionResult{
			{
				Action:      yt.ActionAllow,
				ObjectID:    nodeID,
				ObjectName:  ptr.String("node " + p.YPath().String()),
				SubjectID:   user1ID,
				SubjectName: &user1,
			},
			allowUsersResult,
		},
	}

	require.Equal(t, expectedResponse, response)

	// check user2's permission

	response, err = yc.CheckPermission(ctx, user2, yt.PermissionRead, p, &yt.CheckPermissionOptions{
		Columns: []string{"protected_column", "regular_column"},
	})
	require.NoError(t, err)

	expectedResponse = &yt.CheckPermissionResponse{
		CheckPermissionResult: allowUsersResult,
		Columns: []yt.CheckPermissionResult{
			{
				Action: yt.ActionDeny,
			},
			allowUsersResult,
		},
	}

	require.Equal(t, expectedResponse, response)
}
